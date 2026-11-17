#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Generate the Life_GPS C protocol header from protocol/lifegps_protocol.yaml.

The YAML is the single source of truth (META-04). This generator is
deterministic and idempotent: the same YAML always produces byte-identical
output, with no timestamps, host paths or environment data in the result.

Usage:
    python3 tools/protocol-gen/gen_c.py            # write the header
    python3 tools/protocol-gen/gen_c.py --check    # 0 = up to date, 1 = drift

Output: Firmware/include/generated/lifegps_protocol.h
"""

from __future__ import annotations

import difflib
import sys
from pathlib import Path
from typing import NoReturn

try:
    import yaml
except ImportError:  # pragma: no cover
    sys.exit("PyYAML is missing. Install: pip install -r tools/protocol-gen/requirements.txt")

ROOT = Path(__file__).resolve().parents[2]
PROTOCOL = ROOT / "protocol" / "lifegps_protocol.yaml"
OUTPUT = ROOT / "Firmware" / "include" / "generated" / "lifegps_protocol.h"

GUARD = "LIFEGPS_GENERATED_PROTOCOL_H_"
BANNER = (
    "generated from protocol/lifegps_protocol.yaml by "
    "tools/protocol-gen/gen_c.py - do not edit"
)
MAX_COLUMNS = 100

TYPE_SIZES = {
    "u8": 1, "i8": 1,
    "u16": 2, "i16": 2,
    "u32": 4, "i32": 4,
    "u64": 8, "i64": 8,
}

TYPE_C = {
    "u8": "uint8_t", "i8": "int8_t",
    "u16": "uint16_t", "i16": "int16_t",
    "u32": "uint32_t", "i32": "int32_t",
    "u64": "uint64_t", "i64": "int64_t",
}


def fail(message: str) -> NoReturn:
    """Abort generation with a diagnostic on standard error."""
    sys.exit(f"gen_c: {message}")


def load(path: Path) -> dict:
    """Load the protocol document, requiring a mapping at the root."""
    if not path.exists():
        fail(f"protocol file not found: {path.relative_to(ROOT)}")
    with path.open(encoding="utf-8") as handle:
        doc = yaml.safe_load(handle)
    if not isinstance(doc, dict):
        fail("protocol root must be a mapping")
    return doc


def macro(value: str) -> str:
    """Turn a YAML key into an upper-case macro suffix."""
    return value.upper().replace("-", "_")


def hex_literal(value: int) -> str:
    """Render an integer as a C hex literal with a stable width."""
    if value < 0:
        fail(f"negative value {value} cannot be rendered as a hex literal")
    if value <= 0xFF:
        return f"0x{value:02X}"
    if value <= 0xFFFF:
        return f"0x{value:04X}"
    if value <= 0xFFFFFFFF:
        return f"0x{value:08X}"
    return f"0x{value:016X}"


def bool_literal(value: object) -> str:
    """Render a YAML boolean as 1 or 0."""
    return "1" if value is True else "0"


def field_layout(owner: str, fields: list, expected_size: int) -> list[tuple[str, str, int, int]]:
    """Return (name, c_type, offset, size) tuples and check contiguity."""
    layout: list[tuple[str, str, int, int]] = []
    cursor = 0
    for field in fields or []:
        if not isinstance(field, dict):
            fail(f"{owner}: every field must be a mapping")
        name = field.get("name")
        field_type = field.get("type")
        if not isinstance(name, str) or not name:
            fail(f"{owner}: field without a name")
        if field_type not in TYPE_SIZES:
            fail(f"{owner}.{name}: unknown type {field_type!r}")
        offset = field.get("offset", cursor)
        size = TYPE_SIZES[field_type]
        if field.get("size", size) != size:
            fail(f"{owner}.{name}: declared size does not match {field_type}")
        if offset != cursor:
            fail(f"{owner}.{name}: offset {offset} breaks contiguity (expected {cursor})")
        layout.append((name, TYPE_C[field_type], offset, size))
        cursor += size
    if cursor != expected_size:
        fail(f"{owner}: fields cover {cursor} bytes, header declares {expected_size}")
    return layout


def emit_version(lines: list[str], meta: dict) -> None:
    """Emit version and meta scalars."""
    version = str(meta.get("version", ""))
    parts = version.split(".")
    if len(parts) != 3 or not all(part.isdigit() for part in parts):
        fail(f"meta.version {version!r} is not <major>.<minor>.<patch>")
    major, minor, patch = (int(part) for part in parts)

    lines.append(f"/* meta: {meta.get('name', '')} ({meta.get('status', '')}). */")
    lines.append(f'#define LGPS_PROTO_NAME "{meta.get("name", "")}"')
    lines.append(f'#define LGPS_PROTO_VERSION_STRING "{version}"')
    lines.append(f"#define LGPS_PROTO_VERSION_MAJOR {major}")
    lines.append(f"#define LGPS_PROTO_VERSION_MINOR {minor}")
    lines.append(f"#define LGPS_PROTO_VERSION_PATCH {patch}")
    endianness = meta.get("endianness")
    if endianness != "little":
        fail(f"meta.endianness must be 'little', got {endianness!r}")
    lines.append("#define LGPS_PROTO_LITTLE_ENDIAN 1")
    for key, name in (
        ("mtu", "MTU"),
        ("max_command_payload", "MAX_COMMAND_PAYLOAD"),
        ("max_data_payload", "MAX_DATA_PAYLOAD"),
    ):
        if not isinstance(meta.get(key), int):
            fail(f"meta.{key} must be an integer")
        lines.append(f"#define LGPS_PROTO_{name} {meta[key]}")
    lines.append("")


def emit_crc(lines: list[str], crc: dict) -> None:
    """Emit checksum parameters for record, frame and package CRCs."""
    for name in sorted(crc):
        entry = crc[name]
        if not isinstance(entry, dict):
            fail(f"crc.{name} must be a mapping")
        prefix = f"LGPS_PROTO_CRC_{macro(name)}_"
        lines.append(f"/* {name} checksum: {entry.get('algorithm', '')}. */")
        lines.append(f'#define {prefix}ALGORITHM "{entry.get("algorithm", "")}"')
        for key, name_upper in (
            ("poly", "POLY"),
            ("init", "INIT"),
            ("xorout", "XOROUT"),
        ):
            if not isinstance(entry.get(key), int):
                fail(f"crc.{name}.{key} must be an integer")
            lines.append(f"#define {prefix}{name_upper} {hex_literal(entry[key])}")
        for key, name_upper in (("refin", "REFIN"), ("refout", "REFOUT")):
            if not isinstance(entry.get(key), bool):
                fail(f"crc.{name}.{key} must be a boolean")
            lines.append(f"#define {prefix}{name_upper} {bool_literal(entry[key])}")
    lines.append("")


def emit_frames(lines: list[str], frames: dict) -> None:
    """Emit magic, sizes and header field offsets for every frame."""
    if not frames:
        fail("frames block is empty")
    for name in sorted(frames):
        frame = frames[name]
        if not isinstance(frame, dict):
            fail(f"frames.{name} must be a mapping")
        prefix = f"LGPS_FRAME_{macro(name)}_"
        magic = frame.get("magic")
        header_size = frame.get("header_size")
        trailer_size = frame.get("trailer_size")
        if not isinstance(magic, int) or not isinstance(header_size, int):
            fail(f"frames.{name} needs integer magic and header_size")
        if not isinstance(trailer_size, int):
            fail(f"frames.{name} needs an integer trailer_size")
        layout = field_layout(f"frames.{name}", frame.get("fields") or [], header_size)

        lines.append(f"/* {name} frame. */")
        lines.append(f"#define {prefix}MAGIC {hex_literal(magic)}")
        lines.append(f"#define {prefix}HEADER_SIZE {header_size}")
        lines.append(f"#define {prefix}TRAILER_SIZE {trailer_size}")
        lines.append(f"#define {prefix}FIELD_COUNT {len(layout)}")
        for field_name, _c_type, offset, size in layout:
            lines.append(f"#define {prefix}OFF_{macro(field_name)} {offset}")
            lines.append(f"#define {prefix}SIZE_{macro(field_name)} {size}")
        lines.append("")


def emit_records(lines: list[str], records: dict) -> None:
    """Emit packed record structs, size macros, field offsets and assertions."""
    if not records:
        fail("records block is empty")
    for name in sorted(records):
        record = records[name]
        if not isinstance(record, dict):
            fail(f"records.{name} must be a mapping")
        size = record.get("size")
        if not isinstance(size, int):
            fail(f"records.{name}.size must be an integer")
        if size <= 0:
            continue
        layout = field_layout(f"records.{name}", record.get("fields") or [], size)
        struct_name = f"lifegps_record_{name}"
        prefix = f"LGPS_RECORD_{macro(name)}_"

        lines.append(f"/* {name} record: {size} B, CRC from crc.{record.get('crc', '?')}. */")
        lines.append("#pragma pack(push, 1)")
        lines.append(f"struct {struct_name} {{")
        for field_name, c_type, offset, _field_size in layout:
            lines.append(f"\t{c_type} {field_name}; /* offset {offset} */")
        lines.append("};")
        lines.append("#pragma pack(pop)")
        lines.append("")
        lines.append(f"#define {prefix}SIZE {size}")
        lines.append(f"#define {prefix}FIELD_COUNT {len(layout)}")
        for field_name, _c_type, offset, _field_size in layout:
            lines.append(f"#define {prefix}OFF_{macro(field_name)} {offset}")
        lines.append("")
        lines.append(f"_Static_assert(sizeof(struct {struct_name}) == {prefix}SIZE,")
        lines.append(f'\t       "{name} record layout does not match the YAML");')
        lines.append("")


def emit_packages(lines: list[str], packages: dict) -> None:
    """Emit magic, sizes, codec string and header/footer offsets per package."""
    for name in sorted(packages):
        package = packages[name]
        if not isinstance(package, dict):
            fail(f"packages.{name} must be a mapping")
        prefix = f"LGPS_PACKAGE_{macro(name)}_"
        magic_ascii = package.get("magic_ascii")
        header_size = package.get("header_size")
        footer_size = package.get("footer_size")
        codec = package.get("codec")
        if not isinstance(magic_ascii, str) or not magic_ascii:
            fail(f"packages.{name}.magic_ascii must be a string")
        if not isinstance(header_size, int) or not isinstance(footer_size, int):
            fail(f"packages.{name} needs integer header_size and footer_size")
        if not isinstance(codec, str) or not codec:
            fail(f"packages.{name}.codec must be a string")

        header_layout: list[tuple[str, str, int, int]] = []
        if "header_fields" in package:
            header_layout = field_layout(
                f"packages.{name}.header", package.get("header_fields") or [], header_size
            )

        footer_layout: list[tuple[str, str, int, int]] = []
        if "footer_fields" in package:
            footer_layout = field_layout(
                f"packages.{name}.footer", package.get("footer_fields") or [], footer_size
            )
        elif footer_size > 0:
            fail(f"packages.{name}: footer_size > 0 without footer_fields")

        lines.append(f"/* {name} package: header {header_size} B, footer {footer_size} B. */")
        lines.append(f'#define {prefix}MAGIC_ASCII "{magic_ascii}"')
        lines.append(f"#define {prefix}MAGIC_LEN {len(magic_ascii)}")
        lines.append(f"#define {prefix}HEADER_SIZE {header_size}")
        lines.append(f"#define {prefix}FOOTER_SIZE {footer_size}")
        lines.append(f'#define {prefix}CODEC "{codec}"')
        for field_name, _c_type, offset, _field_size in header_layout:
            lines.append(f"#define {prefix}HEADER_OFF_{macro(field_name)} {offset}")
        for field_name, _c_type, offset, _field_size in footer_layout:
            lines.append(f"#define {prefix}FOOTER_OFF_{macro(field_name)} {offset}")
        lines.append("")


def emit_enums(lines: list[str], doc: dict) -> None:
    """Emit the opcode, stream id and telemetry id enumerations."""
    streams = doc.get("streams") or []
    if streams:
        lines.append("enum lgps_stream_id {")
        for stream in streams:
            if not isinstance(stream, dict):
                fail("streams entries must be mappings")
            name = stream.get("name")
            stream_id = stream.get("id")
            if not isinstance(name, str) or not isinstance(stream_id, int):
                fail("stream entries need a name and an integer id")
            lines.append(f"\tLGPS_STREAM_{macro(name)} = {hex_literal(stream_id)},")
        lines.append("};")
        lines.append("")

    commands = doc.get("commands") or []
    if commands:
        lines.append("enum lgps_cmd_opcode {")
        for command in commands:
            if not isinstance(command, dict):
                fail("commands entries must be mappings")
            name = command.get("name")
            opcode = command.get("opcode")
            if not isinstance(name, str) or not isinstance(opcode, int):
                fail("command entries need a name and an integer opcode")
            lines.append(f"\tLGPS_CMD_{macro(name)} = {hex_literal(opcode)},")
        lines.append("};")
        lines.append("")

    telemetry = doc.get("telemetry") or []
    if telemetry:
        lines.append("enum lgps_telemetry_id {")
        for item in telemetry:
            if not isinstance(item, dict):
                fail("telemetry entries must be mappings")
            name = item.get("name")
            item_id = item.get("id")
            if not isinstance(name, str) or not isinstance(item_id, int):
                fail("telemetry entries need a name and an integer id")
            lines.append(f"\tLGPS_TELEMETRY_{macro(name)} = {hex_literal(item_id)},")
        lines.append("};")
        lines.append("")


def emit_uuids(lines: list[str], uuids: dict) -> None:
    """Emit one string macro per declared UUID."""
    if not uuids:
        fail("uuids block is empty")
    for name in sorted(uuids):
        value = uuids[name]
        if not isinstance(value, str):
            fail(f"uuids.{name} must be a string")
        lines.append(f'#define LGPS_UUID_{macro(name)} "{value}"')
    lines.append("")


def render(doc: dict) -> str:
    """Render the complete header as text."""
    meta = doc.get("meta")
    if not isinstance(meta, dict):
        fail("meta block is missing")

    lines: list[str] = [
        "/*",
        " * SPDX-License-Identifier: MIT",
        " *",
        " * Life_GPS BLE protocol - C constants and record layouts.",
        " *",
        f" * {BANNER}",
        " *",
        " * Byte order is little-endian for every multi-byte field.",
        " */",
        "",
        f"#ifndef {GUARD}",
        f"#define {GUARD}",
        "",
        "#include <stdint.h>",
        "",
    ]

    emit_version(lines, meta)

    crc = doc.get("crc")
    if not isinstance(crc, dict) or not crc:
        fail("crc block is missing")
    emit_crc(lines, crc)

    frames = doc.get("frames") or {}
    emit_frames(lines, frames)

    records = doc.get("records") or {}
    emit_records(lines, records)

    packages = doc.get("packages") or {}
    emit_packages(lines, packages)

    emit_enums(lines, doc)

    uuids = doc.get("uuids") or {}
    emit_uuids(lines, uuids)

    lines.append(f"#endif /* {GUARD} */")
    text = "\n".join(lines)
    return text if text.endswith("\n") else text + "\n"


def check_columns(text: str) -> None:
    """Reject generated lines wider than the Zephyr style limit."""
    for number, line in enumerate(text.splitlines(), start=1):
        if len(line.expandtabs(8)) > MAX_COLUMNS:
            fail(f"generated line {number} exceeds {MAX_COLUMNS} columns")


def show_diff(expected: str, actual: str) -> str:
    """Return a short unified diff between the on-disk and generated text."""
    diff = list(
        difflib.unified_diff(
            actual.splitlines(),
            expected.splitlines(),
            fromfile=str(OUTPUT.relative_to(ROOT)),
            tofile="regenerated",
            lineterm="",
            n=2,
        )
    )
    if len(diff) > 40:
        diff = diff[:40] + ["... (diff truncated)"]
    return "\n".join(diff)


def main(argv: list[str]) -> int:
    """Run the generator, or verify the generated file when --check is passed."""
    check_only = False
    for argument in argv[1:]:
        if argument == "--check":
            check_only = True
        elif argument in ("-h", "--help"):
            print(__doc__)
            return 0
        else:
            fail(f"unknown argument {argument!r} (use --check)")

    text = render(load(PROTOCOL))
    check_columns(text)
    relative_output = OUTPUT.relative_to(ROOT)

    if check_only:
        if not OUTPUT.exists():
            print(f"gen_c: {relative_output} is missing; run the generator")
            return 1
        current = OUTPUT.read_text(encoding="utf-8")
        if current == text:
            print(f"gen_c: {relative_output} is up to date")
            return 0
        print(f"gen_c: {relative_output} is out of date; run: python3 tools/protocol-gen/gen_c.py")
        print(show_diff(text, current))
        return 1

    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text(text, encoding="utf-8", newline="\n")
    print(f"gen_c: wrote {relative_output}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
