#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Shared protocol schema loader, UUID helpers, CRCs and heatshrink codec.

Used by validate.py, gen_c.py, gen_kt.py and gen_vectors.py so every tool
agrees on the exact same YAML interpretation, checksums and compression wire
format.
"""

from __future__ import annotations

import re
import zlib
from pathlib import Path

try:
    import yaml
except ImportError as exc:  # pragma: no cover
    raise SystemExit(
        "PyYAML is missing. Install: pip install -r tools/protocol-gen/requirements.txt"
    ) from exc

ROOT = Path(__file__).resolve().parents[2]
PROTOCOL = ROOT / "protocol" / "lifegps_protocol.yaml"

TYPE_SIZES: dict[str, int] = {
    "u8": 1,
    "i8": 1,
    "u16": 2,
    "i16": 2,
    "u32": 4,
    "i32": 4,
    "u64": 8,
    "i64": 8,
}

TYPE_C: dict[str, str] = {
    "u8": "uint8_t",
    "i8": "int8_t",
    "u16": "uint16_t",
    "i16": "int16_t",
    "u32": "uint32_t",
    "i32": "int32_t",
    "u64": "uint64_t",
    "i64": "int64_t",
}

UUID_RE = re.compile(
    r"^[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-"
    r"[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}$"
)


def load_protocol(path: Path = PROTOCOL) -> dict:
    """Load protocol YAML as a dictionary."""
    if not path.exists():
        raise SystemExit(f"Protocol file not found: {path}")
    with path.open(encoding="utf-8") as handle:
        data = yaml.safe_load(handle)
    if not isinstance(data, dict):
        raise SystemExit("Protocol root must be a mapping")
    return data


def macro_name(value: str) -> str:
    """Turn a YAML identifier into an upper-case macro suffix."""
    return value.upper().replace("-", "_")


def uuid_to_le_bytes(canonical: str) -> list[int]:
    """Convert canonical 128-bit UUID string to BLE wire order (LSB first)."""
    if not UUID_RE.match(canonical):
        raise ValueError(f"Invalid UUID: {canonical!r}")
    raw = bytes.fromhex(canonical.replace("-", ""))
    return list(reversed(raw))


def crc8(data: bytes, poly: int = 0x07, init: int = 0x00) -> int:
    """CRC-8 (MSB-first, no reflection, xorout=0), matching Firmware/src/util/crc.c."""
    crc = init & 0xFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x80:
                crc = ((crc << 1) ^ poly) & 0xFF
            else:
                crc = (crc << 1) & 0xFF
    return crc


def crc16_ccitt_false(data: bytes, poly: int = 0x1021, init: int = 0xFFFF) -> int:
    """CRC-16/CCITT-FALSE, matching Firmware/src/util/crc.c and BleFrame.kt."""
    crc = init & 0xFFFF
    for byte in data:
        crc ^= (byte & 0xFF) << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ poly) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def crc32_iso_hdlc(data: bytes) -> int:
    """CRC-32/ISO-HDLC (zlib), matching Firmware/src/util/crc.c."""
    return zlib.crc32(data) & 0xFFFFFFFF


def byte_delta_encode(raw: bytes, record_size: int = 32) -> bytes:
    """Encode N fixed-size records via byte-wise modulo-256 delta across records."""
    if len(raw) % record_size != 0:
        raise ValueError(f"Input length {len(raw)} is not a multiple of {record_size}")
    if len(raw) <= record_size:
        return bytes(raw)
    out = bytearray(raw[:record_size])
    for off in range(record_size, len(raw), record_size):
        prev = raw[off - record_size : off]
        curr = raw[off : off + record_size]
        for idx in range(record_size):
            out.append((curr[idx] - prev[idx]) & 0xFF)
    return bytes(out)


def byte_delta_decode(encoded: bytes, record_size: int = 32) -> bytes:
    """Inverse of byte_delta_encode()."""
    if len(encoded) % record_size != 0:
        raise ValueError(f"Encoded length {len(encoded)} is not a multiple of {record_size}")
    if len(encoded) <= record_size:
        return bytes(encoded)
    out = bytearray(encoded[:record_size])
    for off in range(record_size, len(encoded), record_size):
        prev_off = off - record_size
        for idx in range(record_size):
            out.append((out[prev_off + idx] + encoded[off + idx]) & 0xFF)
    return bytes(out)


class _BitWriter:
    def __init__(self) -> None:
        self._buf = bytearray()
        self._cur = 0
        self._bits = 0

    def push_bits(self, value: int, count: int) -> None:
        for shift in range(count - 1, -1, -1):
            bit = (value >> shift) & 1
            self._cur = (self._cur << 1) | bit
            self._bits += 1
            if self._bits == 8:
                self._buf.append(self._cur)
                self._cur = 0
                self._bits = 0

    def finish(self) -> bytes:
        if self._bits > 0:
            self._buf.append(self._cur << (8 - self._bits))
            self._cur = 0
            self._bits = 0
        return bytes(self._buf)


class _BitReader:
    def __init__(self, data: bytes) -> None:
        self._data = data
        self._bit_pos = 0
        self._total_bits = len(data) * 8

    def remaining(self) -> int:
        return self._total_bits - self._bit_pos

    def pull_bits(self, count: int) -> int | None:
        if self.remaining() < count:
            return None
        val = 0
        for _ in range(count):
            byte_idx = self._bit_pos >> 3
            bit_idx = 7 - (self._bit_pos & 7)
            val = (val << 1) | ((self._data[byte_idx] >> bit_idx) & 1)
            self._bit_pos += 1
        return val


def heatshrink_encode(data: bytes, window_sz2: int = 8, lookahead_sz2: int = 4) -> bytes:
    """Reference heatshrink LZSS encoder (MSB-first bitstream, zero external deps).

    Wire format matches atomicobject/heatshrink:
      - Literal:       [1][byte: 8 bits]
      - Backreference: [0][dist - 1: window_sz2 bits][len - 1: lookahead_sz2 bits]
    """
    if not (4 <= window_sz2 <= 15) or not (3 <= lookahead_sz2 < window_sz2):
        raise ValueError(f"Invalid heatshrink params: W={window_sz2}, L={lookahead_sz2}")

    window_size = 1 << window_sz2
    max_match = 1 << lookahead_sz2
    # Break-even match length: backref is (1 + W + L) bits vs 9 bits per literal.
    backref_bits = 1 + window_sz2 + lookahead_sz2
    min_match = (backref_bits // 9) + 1

    writer = _BitWriter()
    pos = 0
    n = len(data)

    while pos < n:
        best_len = 0
        best_dist = 0
        start = max(0, pos - window_size)
        limit = min(max_match, n - pos)

        # Prefer longest match; tie-break by smallest distance (most recent).
        for cand in range(pos - 1, start - 1, -1):
            if data[cand] != data[pos]:
                continue
            match_len = 1
            while match_len < limit and data[cand + match_len] == data[pos + match_len]:
                match_len += 1
            if match_len > best_len:
                best_len = match_len
                best_dist = pos - cand
                if best_len == limit:
                    break

        if best_len >= min_match:
            writer.push_bits(0, 1)
            writer.push_bits(best_dist - 1, window_sz2)
            writer.push_bits(best_len - 1, lookahead_sz2)
            pos += best_len
        else:
            writer.push_bits(1, 1)
            writer.push_bits(data[pos], 8)
            pos += 1

    return writer.finish()


def heatshrink_decode(
    data: bytes, expected_len: int | None = None, window_sz2: int = 8, lookahead_sz2: int = 4
) -> bytes:
    """Reference heatshrink LZSS decoder matching heatshrink_encode()."""
    if not (4 <= window_sz2 <= 15) or not (3 <= lookahead_sz2 < window_sz2):
        raise ValueError(f"Invalid heatshrink params: W={window_sz2}, L={lookahead_sz2}")

    reader = _BitReader(data)
    out = bytearray()

    while expected_len is None or len(out) < expected_len:
        flag = reader.pull_bits(1)
        if flag is None:
            break
        if flag == 1:
            literal = reader.pull_bits(8)
            if literal is None:
                break
            out.append(literal)
        else:
            dist_m1 = reader.pull_bits(window_sz2)
            len_m1 = reader.pull_bits(lookahead_sz2)
            if dist_m1 is None or len_m1 is None:
                break
            dist = dist_m1 + 1
            length = len_m1 + 1
            if dist > len(out):
                raise ValueError(f"Heatshrink backreference out of range: dist={dist}, out={len(out)}")
            for _ in range(length):
                out.append(out[-dist])
                if expected_len is not None and len(out) == expected_len:
                    break

    if expected_len is not None and len(out) != expected_len:
        raise ValueError(f"Heatshrink decoded {len(out)} bytes, expected {expected_len}")
    return bytes(out)
