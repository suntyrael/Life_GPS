#!/usr/bin/env python3
"""Validate protocol/lifegps_protocol.yaml.

No code generation yet. This gate catches the most common contract mistakes:
- duplicate/invalid UUIDs
- record/frame/package size mismatches
- field offsets not contiguous
- duplicate opcodes/stream ids
- unknown record references

Usage:
    python3 tools/protocol-gen/validate.py
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

try:
    import yaml
except ImportError:  # pragma: no cover
    sys.exit("PyYAML is missing. Install: pip install -r tools/protocol-gen/requirements.txt")

ROOT = Path(__file__).resolve().parents[2]
PROTOCOL = ROOT / "protocol" / "lifegps_protocol.yaml"

TYPE_SIZES = {
    "u8": 1, "i8": 1,
    "u16": 2, "i16": 2,
    "u32": 4, "i32": 4,
    "u64": 8, "i64": 8,
}
UUID_RE = re.compile(
    r"^[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-"
    r"[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}$"
)

errors: list[str] = []


def error(message: str) -> None:
    errors.append(message)


def load(path: Path) -> dict:
    if not path.exists():
        sys.exit(f"Protocol file not found: {path}")
    with path.open(encoding="utf-8") as handle:
        data = yaml.safe_load(handle)
    if not isinstance(data, dict):
        sys.exit("Protocol root must be a mapping")
    return data


def check_meta(doc: dict) -> None:
    meta = doc.get("meta") or {}
    for key in ("name", "version", "endianness", "mtu", "max_command_payload", "max_data_payload"):
        if key not in meta:
            error(f"meta.{key} is missing")
    if meta.get("endianness") != "little":
        error("meta.endianness must be 'little'")
    if not isinstance(meta.get("mtu"), int) or meta.get("mtu", 0) < 23:
        error("meta.mtu must be an integer >= 23")


def check_crc(doc: dict) -> None:
    crc = doc.get("crc") or {}
    for name in ("record", "frame", "package"):
        entry = crc.get(name)
        if not isinstance(entry, dict):
            error(f"crc.{name} is missing")
            continue
        for key in ("algorithm", "poly", "init"):
            if key not in entry:
                error(f"crc.{name}.{key} is missing")


def check_uuids(doc: dict) -> None:
    uuids = doc.get("uuids") or {}
    if not uuids:
        error("uuids is empty")
        return
    seen: dict[str, str] = {}
    for name, value in uuids.items():
        if not isinstance(value, str) or not UUID_RE.match(value):
            error(f"uuid {name!r} is invalid: {value!r}")
            continue
        normalized = value.lower()
        if normalized in seen:
            error(f"uuid {name!r} duplicates {seen[normalized]!r}")
        seen[normalized] = name


def check_fields(owner: str, fields: list | None, expected_size: int | None = None) -> int:
    fields = fields or []
    running = 0
    names: set[str] = set()
    for index, field in enumerate(fields):
        if not isinstance(field, dict):
            error(f"{owner}[{index}] must be a mapping")
            continue
        name = field.get("name")
        field_type = field.get("type")
        offset = field.get("offset", running)
        size = field.get("size")

        if not name:
            error(f"{owner}[{index}] has no name")
        elif name in names:
            error(f"{owner} has duplicate field {name!r}")
        else:
            names.add(name)

        if field_type not in TYPE_SIZES:
            error(f"{owner}.{name} has unknown type {field_type!r}")
            continue

        actual_size = TYPE_SIZES[field_type]
        if size is None:
            size = actual_size
        elif size != actual_size:
            error(f"{owner}.{name} declares size {size}, expected {actual_size}")

        if offset != running:
            error(f"{owner}.{name} offset {offset}, expected {running}")
            running = offset

        running += size

    if expected_size is not None and running != expected_size:
        error(f"{owner} size is {running}, declared {expected_size}")
    return running


def check_records(doc: dict) -> None:
    records = doc.get("records") or {}
    if not records:
        error("records is empty")
    for name, record in records.items():
        if not isinstance(record, dict):
            error(f"records.{name} must be a mapping")
            continue
        check_fields(f"records.{name}", record.get("fields"), record.get("size"))


def check_frames(doc: dict) -> None:
    frames = doc.get("frames") or {}
    if not frames:
        error("frames is empty")
    for name, frame in frames.items():
        if not isinstance(frame, dict):
            error(f"frames.{name} must be a mapping")
            continue
        check_fields(f"frames.{name}", frame.get("fields"), frame.get("header_size"))
        if "trailer_size" not in frame:
            error(f"frames.{name}.trailer_size is missing")
        if "magic" not in frame:
            error(f"frames.{name}.magic is missing")


def check_packages(doc: dict) -> None:
    packages = doc.get("packages") or {}
    for name, package in packages.items():
        if not isinstance(package, dict):
            error(f"packages.{name} must be a mapping")
            continue
        check_fields(
            f"packages.{name}.header",
            package.get("header_fields"),
            package.get("header_size"),
        )
        if "footer_size" in package:
            check_fields(
                f"packages.{name}.footer",
                package.get("footer_fields"),
                package.get("footer_size"),
            )


def check_streams(doc: dict) -> None:
    records = doc.get("records") or {}
    seen: set[int] = set()
    for stream in doc.get("streams") or []:
        stream_id = stream.get("id")
        name = stream.get("name", "<unnamed>")
        if not isinstance(stream_id, int) or not 0 <= stream_id <= 0xFF:
            error(f"stream {name!r} has invalid id {stream_id!r}")
            continue
        if stream_id in seen:
            error(f"stream id {stream_id} is duplicated")
        seen.add(stream_id)
        record = stream.get("record")
        if record not in records:
            error(f"stream {name!r} references unknown record {record!r}")


def check_commands(doc: dict) -> None:
    opcodes: set[int] = set()
    names: set[str] = set()
    for command in doc.get("commands") or []:
        opcode = command.get("opcode")
        name = command.get("name", "<unnamed>")
        if not isinstance(opcode, int) or not 0 <= opcode <= 0xFF:
            error(f"command {name!r} has invalid opcode {opcode!r}")
        elif opcode in opcodes:
            error(f"opcode 0x{opcode:02X} is duplicated")
        else:
            opcodes.add(opcode)

        if name in names:
            error(f"command name {name!r} is duplicated")
        names.add(name)

        direction = command.get("direction")
        if direction not in ("app_to_dev", "dev_to_app", "bidirectional"):
            error(f"command {name!r} has invalid direction {direction!r}")


def check_telemetry(doc: dict) -> None:
    seen: set[int] = set()
    for item in doc.get("telemetry") or []:
        item_id = item.get("id")
        name = item.get("name", "<unnamed>")
        if not isinstance(item_id, int) or not 0 <= item_id <= 0xFF:
            error(f"telemetry {name!r} has invalid id {item_id!r}")
            continue
        if item_id in seen:
            error(f"telemetry id 0x{item_id:02X} is duplicated")
        seen.add(item_id)


def check_day_package_extras(doc: dict) -> None:
    day = (doc.get("packages") or {}).get("day") or {}
    if not isinstance(day, dict):
        return
    hs = day.get("heatshrink")
    if hs is not None:
        if not isinstance(hs, dict):
            error("packages.day.heatshrink must be a mapping")
        else:
            w = hs.get("window_sz2")
            la = hs.get("lookahead_sz2")
            if not isinstance(w, int) or not (4 <= w <= 15):
                error(f"packages.day.heatshrink.window_sz2 invalid: {w!r}")
            if not isinstance(la, int) or not (3 <= la < (w if isinstance(w, int) else 0)):
                error(f"packages.day.heatshrink.lookahead_sz2 invalid: {la!r}")
    codec_values = day.get("codec_values")
    if codec_values is not None:
        if not isinstance(codec_values, dict) or not codec_values:
            error("packages.day.codec_values must be a non-empty mapping")
        else:
            seen_vals: set[int] = set()
            for cname, cval in codec_values.items():
                if not isinstance(cval, int) or not 0 <= cval <= 0xFFFF:
                    error(f"packages.day.codec_values.{cname} invalid: {cval!r}")
                elif cval in seen_vals:
                    error(f"packages.day.codec_values duplicates value {cval}")
                else:
                    seen_vals.add(cval)


def check_status_codes(doc: dict) -> None:
    codes = doc.get("status_codes")
    if codes is None:
        return
    seen_codes: set[int] = set()
    seen_names: set[str] = set()
    for item in codes:
        if not isinstance(item, dict):
            error("status_codes entry must be a mapping")
            continue
        code = item.get("code")
        name = item.get("name", "<unnamed>")
        if not isinstance(code, int) or not 0 <= code <= 0xFF:
            error(f"status_code {name!r} has invalid code {code!r}")
        elif code in seen_codes:
            error(f"status_code 0x{code:02X} is duplicated")
        else:
            seen_codes.add(code)
        if name in seen_names:
            error(f"status_code name {name!r} is duplicated")
        seen_names.add(name)


def check_caps_bits(doc: dict) -> None:
    caps = doc.get("caps_bits")
    if caps is None:
        return
    seen_bits: set[int] = set()
    seen_names: set[str] = set()
    for item in caps:
        if not isinstance(item, dict):
            error("caps_bits entry must be a mapping")
            continue
        bit = item.get("bit")
        name = item.get("name", "<unnamed>")
        if not isinstance(bit, int) or not 0 <= bit <= 31:
            error(f"caps_bits {name!r} has invalid bit {bit!r}")
        elif bit in seen_bits:
            error(f"caps_bits bit {bit} is duplicated")
        else:
            seen_bits.add(bit)
        if name in seen_names:
            error(f"caps_bits name {name!r} is duplicated")
        seen_names.add(name)


def check_config_tlv(doc: dict) -> None:
    tlvs = doc.get("config_tlv")
    if tlvs is None:
        return
    seen_keys: set[int] = set()
    seen_names: set[str] = set()
    for item in tlvs:
        if not isinstance(item, dict):
            error("config_tlv entry must be a mapping")
            continue
        key = item.get("key")
        name = item.get("name", "<unnamed>")
        ftype = item.get("type")
        if not isinstance(key, int) or not 1 <= key <= 0xFF:
            error(f"config_tlv {name!r} has invalid key {key!r}")
        elif key in seen_keys:
            error(f"config_tlv key 0x{key:02X} is duplicated")
        else:
            seen_keys.add(key)
        if name in seen_names:
            error(f"config_tlv name {name!r} is duplicated")
        seen_names.add(name)
        if ftype not in TYPE_SIZES:
            error(f"config_tlv {name!r} has unknown type {ftype!r}")


def check_adv_manufacturer_data(doc: dict) -> None:
    adv = doc.get("adv_manufacturer_data")
    if adv is None:
        return
    if not isinstance(adv, dict):
        error("adv_manufacturer_data must be a mapping")
        return
    if not isinstance(adv.get("company_id"), int) or not 0 <= adv["company_id"] <= 0xFFFF:
        error(f"adv_manufacturer_data.company_id invalid: {adv.get('company_id')!r}")
    check_fields("adv_manufacturer_data", adv.get("fields"), adv.get("size"))


def check_status_extension(doc: dict) -> None:
    ext = doc.get("status_extension")
    if ext is None:
        return
    if not isinstance(ext, dict):
        error("status_extension must be a mapping")
        return
    acc_size = ext.get("accuracy_size")
    sync_size = ext.get("sync_size")
    if not isinstance(acc_size, int) or not isinstance(sync_size, int) or sync_size < acc_size:
        error("status_extension sizes are invalid")
        return
    cursor = acc_size
    for field in ext.get("sync_fields") or []:
        name = field.get("name", "<unnamed>")
        ftype = field.get("type")
        offset = field.get("offset")
        if ftype not in TYPE_SIZES:
            error(f"status_extension.{name} has unknown type {ftype!r}")
            continue
        if offset != cursor:
            error(f"status_extension.{name} offset {offset}, expected {cursor}")
        cursor += TYPE_SIZES[ftype]
    if cursor != sync_size:
        error(f"status_extension sync_fields end at {cursor}, declared {sync_size}")


def main() -> int:
    doc = load(PROTOCOL)
    check_meta(doc)
    check_crc(doc)
    check_uuids(doc)
    check_records(doc)
    check_frames(doc)
    check_packages(doc)
    check_day_package_extras(doc)
    check_streams(doc)
    check_commands(doc)
    check_telemetry(doc)
    check_status_codes(doc)
    check_caps_bits(doc)
    check_config_tlv(doc)
    check_adv_manufacturer_data(doc)
    check_status_extension(doc)

    if errors:
        print("Protocol validation FAILED:")
        for item in errors:
            print(f"  - {item}")
        return 1

    print(
        "Protocol OK: "
        f"{len(doc.get('records') or {})} records, "
        f"{len(doc.get('uuids') or {})} UUIDs, "
        f"{len(doc.get('commands') or [])} commands, "
        f"{len(doc.get('streams') or [])} streams, "
        f"{len(doc.get('status_codes') or [])} status_codes, "
        f"{len(doc.get('config_tlv') or [])} config_tlv"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
