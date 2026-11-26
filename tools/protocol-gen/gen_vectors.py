#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Generate deterministic cross-language Golden Vectors for C and Kotlin tests.

Outputs:
  - protocol/vectors/*.bin
  - protocol/vectors/manifest.json
  - Firmware/tests/lifegps.protogen/src/generated_vectors.h

Usage:
    python3 tools/protocol-gen/gen_vectors.py            # write vectors
    python3 tools/protocol-gen/gen_vectors.py --check    # 0 = up to date, 1 = drift
"""

from __future__ import annotations

import hashlib
import json
import struct
import sys
from pathlib import Path

from schema import (
    PROTOCOL,
    ROOT,
    byte_delta_decode,
    byte_delta_encode,
    crc8,
    crc16_ccitt_false,
    crc32_iso_hdlc,
    heatshrink_decode,
    heatshrink_encode,
    load_protocol,
)

VECTORS_DIR = ROOT / "protocol" / "vectors"
MANIFEST_PATH = VECTORS_DIR / "manifest.json"
C_HEADER_PATH = (
    ROOT / "Firmware" / "tests" / "lifegps.protogen" / "src" / "generated_vectors.h"
)
MAX_COLUMNS = 100


def pack_command_frame(opcode: int, flags: int, seq: int, payload: bytes) -> bytes:
    header = struct.pack("<BBBBHH", 0xA5, 0x01, opcode, flags, seq, len(payload))
    body = header + payload
    crc = crc16_ccitt_false(body)
    return body + struct.pack("<H", crc)


def pack_data_frame(
    stream: int, flags: int, transfer_id: int, cursor: int, payload: bytes
) -> bytes:
    header = struct.pack(
        "<BBBBIIH", 0x5A, 0x01, stream, flags, transfer_id, cursor, len(payload)
    )
    body = header + payload
    crc = crc16_ccitt_false(body)
    return body + struct.pack("<H", crc)


def pack_trajectory_record(
    seq_id: int,
    timestamp: int,
    subsecond_ms: int,
    latitude: int,
    longitude: int,
    altitude: int,
    speed_cms: int,
    heading: int,
    step_count: int,
    hdop: int,
    satellites: int,
    status_flags: int,
    battery_level: int,
    floor_level: int,
) -> bytes:
    body = struct.pack(
        "<IIHiihHHHBBBBb",
        seq_id,
        timestamp,
        subsecond_ms,
        latitude,
        longitude,
        altitude,
        speed_cms,
        heading,
        step_count,
        hdop,
        satellites,
        status_flags,
        battery_level,
        floor_level,
    )
    assert len(body) == 31
    return body + bytes([crc8(body)])


def pack_day_package(
    day_id: int,
    prev_day_id: int,
    raw_records: bytes,
    codec_id: int,
    window_sz2: int,
    lookahead_sz2: int,
) -> bytes:
    assert len(raw_records) % 32 == 0
    record_count = len(raw_records) // 32
    if codec_id == 0:
        packed = raw_records
    elif codec_id == 1:
        delta = byte_delta_encode(raw_records, 32)
        packed = heatshrink_encode(delta, window_sz2=window_sz2, lookahead_sz2=lookahead_sz2)
        # Self-check round-trip immediately while generating.
        restored_delta = heatshrink_decode(
            packed, expected_len=len(raw_records), window_sz2=window_sz2, lookahead_sz2=lookahead_sz2
        )
        restored = byte_delta_decode(restored_delta, 32)
        assert restored == raw_records, "heatshrink + byte-delta round-trip mismatch"
    else:
        raise ValueError(f"Unsupported codec_id: {codec_id}")

    header_46 = struct.pack(
        "<4sHHIIIIIIIIIH",
        b"LGD1",
        1,
        codec_id,
        day_id,
        record_count,
        0,
        len(raw_records),
        len(packed),
        0,
        0,
        prev_day_id,
        0,
        0,
    )
    assert len(header_46) == 46
    header = header_46 + struct.pack("<H", crc16_ccitt_false(header_46))
    footer = struct.pack("<II", crc32_iso_hdlc(raw_records), 0)
    return header + packed + footer


def build_vectors(doc: dict) -> tuple[dict[str, bytes], dict]:
    hs = doc["packages"]["day"]["heatshrink"]
    w_sz2 = int(hs["window_sz2"])
    l_sz2 = int(hs["lookahead_sz2"])

    vectors: dict[str, bytes] = {}
    meta: dict[str, dict] = {}

    # 1. Standard CRC reference vector ("123456789").
    crc_ref = b"123456789"
    vectors["crc_reference.bin"] = crc_ref
    meta["crc_reference.bin"] = {
        "crc8": f"0x{crc8(crc_ref):02X}",
        "crc16": f"0x{crc16_ccitt_false(crc_ref):04X}",
        "crc32": f"0x{crc32_iso_hdlc(crc_ref):08X}",
    }

    # 2. Command frame request (GET_CAPS, seq=1) & response (status=OK + 24B caps).
    cmd_req = pack_command_frame(opcode=0x01, flags=0x00, seq=1, payload=b"")
    vectors["cmd_get_caps_req.bin"] = cmd_req
    meta["cmd_get_caps_req.bin"] = {"opcode": 0x01, "flags": 0x00, "seq": 1, "payload_len": 0}

    caps_body = struct.pack(
        "<BBBBBBBBHHIIHH",
        1, 1, 0, 0,       # wire_ver=1, proto 1.0.0
        0, 5, 0, 0,       # fw 0.5.0, reserved=0
        247, 235,         # att_mtu=247, max_cmd_payload=235
        0x00000FFF,       # caps bit0..bit11
        0x00000336,       # telemetry_mask (GNSS/IMU/BARO/PDR/NMEA/SAT)
        116, 0,           # status_size=116, reserved2=0
    )
    assert len(caps_body) == 24
    cmd_rsp = pack_command_frame(opcode=0x01, flags=0x01, seq=1, payload=b"\x00" + caps_body)
    vectors["cmd_get_caps_rsp.bin"] = cmd_rsp
    meta["cmd_get_caps_rsp.bin"] = {"opcode": 0x01, "flags": 0x01, "seq": 1, "status": 0}

    # 3. Telemetry 0x09 SAT legacy 5-byte row edge case: 7 sats -> body = 35 B (divisible by 7 AND 5!).
    sat_hdr = struct.pack("<BBH", 7, 7, 0)
    sat_rows = bytearray()
    for i in range(7):
        sat_rows += struct.pack("<BBBbb", 0, 10 + i, 30 + i, 20 + i * 5, 0x09)
    sat_legacy_35 = sat_hdr + bytes(sat_rows)
    assert len(sat_legacy_35) == 4 + 35
    vectors["telem_sat_legacy_7sats.bin"] = sat_legacy_35
    meta["telem_sat_legacy_7sats.bin"] = {"count": 7, "row_bytes": 5, "body_bytes": 35}

    # 4. Trajectory records (3 records covering negative coords, negative altitude, negative floor, POI).
    r0 = pack_trajectory_record(
        seq_id=100, timestamp=210000000, subsecond_ms=250,
        latitude=312304160, longitude=1214737010, altitude=18,
        speed_cms=135, heading=875, step_count=42,
        hdop=12, satellites=14, status_flags=0x05, battery_level=88, floor_level=0,
    )
    r1 = pack_trajectory_record(
        seq_id=101, timestamp=210000060, subsecond_ms=500,
        latitude=-338688200, longitude=-1512092960, altitude=-12,
        speed_cms=0, heading=3599, step_count=42,
        hdop=25, satellites=9, status_flags=0xA3, battery_level=87, floor_level=-2,
    )
    r2 = pack_trajectory_record(
        seq_id=102, timestamp=210000120, subsecond_ms=999,
        latitude=312304990, longitude=1214738120, altitude=420,
        speed_cms=1500, heading=1800, step_count=118,
        hdop=9, satellites=18, status_flags=0x11, battery_level=86, floor_level=5,
    )
    traj_basic = r0 + r1 + r2
    vectors["trajectory_basic.bin"] = traj_basic
    meta["trajectory_basic.bin"] = {
        "record_count": 3,
        "crc32": f"0x{crc32_iso_hdlc(traj_basic):08X}",
    }

    # 5. Trajectory records exercising 0xFF <-> 0x00 byte-delta wraparound (8 records).
    wrap_recs = bytearray()
    for idx in range(8):
        wrap_recs += pack_trajectory_record(
            seq_id=0x000000FC + idx,
            timestamp=210000252 + idx * 60,
            subsecond_ms=(995 + idx * 3) % 1000,
            latitude=312300000 + idx * 255,
            longitude=1214700000 - idx * 257,
            altitude=100 + idx,
            speed_cms=120 + idx,
            heading=(3590 + idx * 5) % 3600,
            step_count=250 + idx * 3,
            hdop=11,
            satellites=15,
            status_flags=0x01,
            battery_level=90 - (idx // 4),
            floor_level=1,
        )
    traj_wrap = bytes(wrap_recs)
    vectors["trajectory_delta_wrap.bin"] = traj_wrap
    meta["trajectory_delta_wrap.bin"] = {
        "record_count": 8,
        "crc32": f"0x{crc32_iso_hdlc(traj_wrap):08X}",
    }

    # 6. Data frame 0x5A carrying 7 trajectory records (224 B payload, 240 B frame).
    chunk_payload = traj_wrap[: 7 * 32]
    data_chunk = pack_data_frame(
        stream=0x00, flags=0x01, transfer_id=0x0000002A, cursor=0x000000FC, payload=chunk_payload
    )
    assert len(data_chunk) == 14 + 224 + 2
    vectors["data_chunk_trajectory.bin"] = data_chunk
    meta["data_chunk_trajectory.bin"] = {
        "stream": 0, "flags": 1, "transfer_id": 42, "cursor": 252, "payload_len": 224
    }

    # 7. LGD1 Day packages: raw (codec=0) and byte_delta_heatshrink (codec=1).
    day_raw = pack_day_package(
        day_id=20261125, prev_day_id=20261124, raw_records=traj_basic,
        codec_id=0, window_sz2=w_sz2, lookahead_sz2=l_sz2,
    )
    vectors["day_package_raw.bin"] = day_raw
    meta["day_package_raw.bin"] = {
        "day_id": 20261125,
        "codec": 0,
        "record_count": 3,
        "raw_len": len(traj_basic),
        "packed_len": len(traj_basic),
        "traj_crc32": f"0x{crc32_iso_hdlc(traj_basic):08X}",
    }

    day_hs = pack_day_package(
        day_id=20261126, prev_day_id=20261125, raw_records=traj_wrap,
        codec_id=1, window_sz2=w_sz2, lookahead_sz2=l_sz2,
    )
    vectors["day_package_hs.bin"] = day_hs
    meta["day_package_hs.bin"] = {
        "day_id": 20261126,
        "codec": 1,
        "window_sz2": w_sz2,
        "lookahead_sz2": l_sz2,
        "record_count": 8,
        "raw_len": len(traj_wrap),
        "packed_len": len(day_hs) - 48 - 8,
        "traj_crc32": f"0x{crc32_iso_hdlc(traj_wrap):08X}",
    }

    manifest = {
        "protocol_version": doc["meta"]["version"],
        "vectors": {
            name: {
                "size": len(blob),
                "sha256": hashlib.sha256(blob).hexdigest(),
                **meta[name],
            }
            for name, blob in sorted(vectors.items())
        },
    }
    return vectors, manifest


def render_c_header(vectors: dict[str, bytes]) -> str:
    """Render C arrays for native_sim protogen tests (strict <= 100 columns)."""
    lines: list[str] = [
        "/*",
        " * SPDX-License-Identifier: MIT",
        " *",
        " * Generated golden protocol vectors for native_sim contract tests.",
        " * generated by tools/protocol-gen/gen_vectors.py - do not edit",
        " */",
        "",
        "#ifndef LIFEGPS_GENERATED_VECTORS_H_",
        "#define LIFEGPS_GENERATED_VECTORS_H_",
        "",
        "#include <stdint.h>",
        "",
    ]

    selected = (
        ("crc_reference.bin", "lgps_vec_crc_ref"),
        ("cmd_get_caps_req.bin", "lgps_vec_cmd_caps_req"),
        ("cmd_get_caps_rsp.bin", "lgps_vec_cmd_caps_rsp"),
        ("trajectory_basic.bin", "lgps_vec_traj_basic"),
        ("data_chunk_trajectory.bin", "lgps_vec_data_chunk"),
        ("day_package_raw.bin", "lgps_vec_day_raw"),
        ("day_package_hs.bin", "lgps_vec_day_hs"),
    )

    for filename, sym in selected:
        blob = vectors[filename]
        lines.append(f"/* {filename} ({len(blob)} bytes) */")
        lines.append(f"static const uint8_t {sym}[{len(blob)}] = {{")
        for i in range(0, len(blob), 12):
            row = ", ".join(f"0x{b:02X}" for b in blob[i : i + 12])
            lines.append(f"\t{row},")
        lines.append("};")
        lines.append("")

    lines.append("#endif /* LIFEGPS_GENERATED_VECTORS_H_ */")
    text = "\n".join(lines) + "\n"
    for num, line in enumerate(text.splitlines(), start=1):
        if len(line.expandtabs(8)) > MAX_COLUMNS:
            sys.exit(f"gen_vectors: C header line {num} exceeds {MAX_COLUMNS} columns")
    return text


def main(argv: list[str]) -> int:
    check_only = False
    for arg in argv[1:]:
        if arg == "--check":
            check_only = True
        elif arg in ("-h", "--help"):
            print(__doc__)
            return 0
        else:
            sys.exit(f"gen_vectors: unknown argument {arg!r} (use --check)")

    doc = load_protocol(PROTOCOL)
    vectors, manifest = build_vectors(doc)
    manifest_text = json.dumps(manifest, indent=2, sort_keys=True) + "\n"
    c_header_text = render_c_header(vectors)

    expected_files: dict[Path, bytes] = {
        VECTORS_DIR / name: blob for name, blob in vectors.items()
    }
    expected_files[MANIFEST_PATH] = manifest_text.encode("utf-8")
    expected_files[C_HEADER_PATH] = c_header_text.encode("utf-8")

    if check_only:
        drift: list[str] = []
        for path, expected in sorted(expected_files.items()):
            rel = path.relative_to(ROOT)
            if not path.exists():
                drift.append(f"missing {rel}")
            elif path.read_bytes() != expected:
                drift.append(f"out of date: {rel}")
        if drift:
            print("gen_vectors: DRIFT detected:")
            for item in drift:
                print(f"  - {item}")
            print("Run: python3 tools/protocol-gen/gen_vectors.py")
            return 1
        print(f"gen_vectors: {len(vectors)} vectors, manifest and C header are up to date")
        return 0

    for path, content in sorted(expected_files.items()):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)

    print(
        f"gen_vectors: wrote {len(vectors)} vectors to {VECTORS_DIR.relative_to(ROOT)}, "
        f"manifest and {C_HEADER_PATH.relative_to(ROOT)}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
