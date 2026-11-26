#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Generate Kotlin protocol constants from protocol/lifegps_protocol.yaml.

Deterministic and idempotent: the same YAML always produces byte-identical
Kotlin source with no timestamps or host paths. Pure JVM (no Android imports).

Usage:
    python3 tools/protocol-gen/gen_kt.py            # write GeneratedProtocol.kt
    python3 tools/protocol-gen/gen_kt.py --check    # 0 = up to date, 1 = drift
"""

from __future__ import annotations

import difflib
import sys
from pathlib import Path

from schema import PROTOCOL, ROOT, TYPE_SIZES, load_protocol, macro_name

OUTPUT = (
    ROOT
    / "App"
    / "app"
    / "src"
    / "main"
    / "java"
    / "com"
    / "lifegps"
    / "app"
    / "ble"
    / "generated"
    / "GeneratedProtocol.kt"
)

BANNER = (
    "Generated from protocol/lifegps_protocol.yaml by "
    "tools/protocol-gen/gen_kt.py - do not edit."
)


def render(doc: dict) -> str:
    meta = doc["meta"]
    version = str(meta["version"])
    major, minor, patch = (int(p) for p in version.split("."))

    lines: list[str] = [
        "// SPDX-License-Identifier: MIT",
        f"// {BANNER}",
        "package com.lifegps.app.ble.generated",
        "",
        "import java.util.UUID",
        "",
        "/**",
        " * Single-source-of-truth BLE protocol constants generated from",
        " * `protocol/lifegps_protocol.yaml`. Multi-byte fields are little-endian.",
        " */",
        "object GeneratedProtocol {",
        "",
        "    object Meta {",
        f'        const val NAME: String = "{meta["name"]}"',
        f'        const val VERSION: String = "{version}"',
        f"        const val VERSION_MAJOR: Int = {major}",
        f"        const val VERSION_MINOR: Int = {minor}",
        f"        const val VERSION_PATCH: Int = {patch}",
        f"        const val MTU: Int = {meta['mtu']}",
        f"        const val MAX_COMMAND_PAYLOAD: Int = {meta['max_command_payload']}",
        f"        const val MAX_DATA_PAYLOAD: Int = {meta['max_data_payload']}",
        "    }",
        "",
        "    object Crc {",
    ]

    crc = doc["crc"]
    for name in sorted(crc):
        entry = crc[name]
        prefix = macro_name(name)
        lines.append(f'        const val {prefix}_ALGORITHM: String = "{entry["algorithm"]}"')
        lines.append(f"        const val {prefix}_POLY: Long = 0x{entry['poly']:X}L")
        lines.append(f"        const val {prefix}_INIT: Long = 0x{entry['init']:X}L")
        lines.append(f"        const val {prefix}_XOROUT: Long = 0x{entry['xorout']:X}L")
        lines.append(f"        const val {prefix}_REFIN: Boolean = {'true' if entry['refin'] else 'false'}")
        lines.append(f"        const val {prefix}_REFOUT: Boolean = {'true' if entry['refout'] else 'false'}")
    lines.append("    }")
    lines.append("")

    lines.append("    object Uuids {")
    uuids = doc["uuids"]
    for name in sorted(uuids):
        lines.append(f'        val {macro_name(name)}: UUID = UUID.fromString("{uuids[name]}")')
    lines.append('        val CCCD: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")')
    lines.append("    }")
    lines.append("")

    for fname in sorted(doc["frames"]):
        frame = doc["frames"][fname]
        obj = "".join(part.capitalize() for part in fname.split("_")) + "Frame"
        lines.append(f"    object {obj} {{")
        lines.append(f"        const val MAGIC: Int = 0x{frame['magic']:02X}")
        lines.append(f"        const val HEADER_SIZE: Int = {frame['header_size']}")
        lines.append(f"        const val TRAILER_SIZE: Int = {frame['trailer_size']}")
        for field in frame["fields"]:
            lines.append(f"        const val OFF_{macro_name(field['name'])}: Int = {field['offset']}")
        lines.append("    }")
        lines.append("")

    traj = doc["records"]["trajectory"]
    lines.append("    object TrajectoryRecord {")
    lines.append(f"        const val SIZE: Int = {traj['size']}")
    for field in traj["fields"]:
        lines.append(f"        const val OFF_{macro_name(field['name'])}: Int = {field['offset']}")
    lines.append("    }")
    lines.append("")

    day = doc["packages"]["day"]
    lines.append("    object DayPackage {")
    lines.append(f'        const val MAGIC_ASCII: String = "{day["magic_ascii"]}"')
    if "header_ver" in day:
        lines.append(f"        const val HEADER_VER: Int = {day['header_ver']}")
    lines.append(f"        const val HEADER_SIZE: Int = {day['header_size']}")
    if "header_crc16_len" in day:
        lines.append(f"        const val HEADER_CRC16_LEN: Int = {day['header_crc16_len']}")
    lines.append(f"        const val FOOTER_SIZE: Int = {day['footer_size']}")
    lines.append(f'        const val CODEC_NAME: String = "{day["codec"]}"')
    for cname in sorted(day.get("codec_values") or {}):
        lines.append(f"        const val CODEC_{macro_name(cname)}: Int = {day['codec_values'][cname]}")
    hs = day.get("heatshrink") or {}
    if hs:
        lines.append(f"        const val HS_WINDOW_SZ2: Int = {hs['window_sz2']}")
        lines.append(f"        const val HS_LOOKAHEAD_SZ2: Int = {hs['lookahead_sz2']}")
    for field in day["header_fields"]:
        lines.append(f"        const val HEADER_OFF_{macro_name(field['name'])}: Int = {field['offset']}")
    for field in day["footer_fields"]:
        lines.append(f"        const val FOOTER_OFF_{macro_name(field['name'])}: Int = {field['offset']}")
    lines.append("    }")
    lines.append("")

    lines.append("    object StreamId {")
    for stream in doc["streams"]:
        lines.append(f"        const val {macro_name(stream['name'])}: Int = 0x{stream['id']:02X}")
    lines.append("    }")
    lines.append("")

    lines.append("    object CommandOpcode {")
    for cmd in doc["commands"]:
        lines.append(f"        const val {macro_name(cmd['name'])}: Int = 0x{cmd['opcode']:02X}")
    lines.append("    }")
    lines.append("")

    lines.append("    object TelemetryId {")
    for item in doc["telemetry"]:
        lines.append(f"        const val {macro_name(item['name'])}: Int = 0x{item['id']:02X}")
    lines.append("    }")
    lines.append("")

    if doc.get("status_codes"):
        lines.append("    object StatusCode {")
        for item in doc["status_codes"]:
            lines.append(f"        const val {macro_name(item['name'])}: Int = 0x{item['code']:02X}")
        lines.append("    }")
        lines.append("")

    if doc.get("caps_bits"):
        lines.append("    object CapsBits {")
        for item in doc["caps_bits"]:
            mask = 1 << item["bit"]
            lines.append(f"        const val {macro_name(item['name'])}: Long = 0x{mask:08X}L")
        lines.append("    }")
        lines.append("")

    if doc.get("config_tlv"):
        lines.append("    object ConfigTlv {")
        for item in doc["config_tlv"]:
            nm = macro_name(item["name"])
            lines.append(f"        const val {nm}: Int = 0x{item['key']:02X}")
            lines.append(f"        const val {nm}_LEN: Int = {TYPE_SIZES[item['type']]}")
        lines.append("    }")
        lines.append("")

    adv = doc.get("adv_manufacturer_data")
    if isinstance(adv, dict):
        lines.append("    object AdvData {")
        lines.append(f"        const val COMPANY_ID: Int = 0x{adv['company_id']:04X}")
        lines.append(f"        const val VERSION: Int = {adv['version']}")
        lines.append(f"        const val SIZE: Int = {adv['size']}")
        for field in adv["fields"]:
            lines.append(f"        const val OFF_{macro_name(field['name'])}: Int = {field['offset']}")
        lines.append("    }")
        lines.append("")

    ext = doc.get("status_extension")
    if isinstance(ext, dict):
        lines.append("    object StatusSizes {")
        lines.append(f"        const val BASE: Int = {ext['base_size']}")
        lines.append(f"        const val MOTION: Int = {ext['motion_size']}")
        lines.append(f"        const val ACCURACY: Int = {ext['accuracy_size']}")
        lines.append(f"        const val SYNC: Int = {ext['sync_size']}")
        for field in ext.get("sync_fields") or []:
            lines.append(f"        const val OFF_{macro_name(field['name'])}: Int = {field['offset']}")
        lines.append("    }")
        lines.append("")

    lines.append("}")
    return "\n".join(lines) + "\n"


def show_diff(expected: str, actual: str) -> str:
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
    check_only = False
    for arg in argv[1:]:
        if arg == "--check":
            check_only = True
        elif arg in ("-h", "--help"):
            print(__doc__)
            return 0
        else:
            sys.exit(f"gen_kt: unknown argument {arg!r} (use --check)")

    text = render(load_protocol(PROTOCOL))
    rel = OUTPUT.relative_to(ROOT)

    if check_only:
        if not OUTPUT.exists():
            print(f"gen_kt: {rel} is missing; run: python3 tools/protocol-gen/gen_kt.py")
            return 1
        current = OUTPUT.read_text(encoding="utf-8")
        if current == text:
            print(f"gen_kt: {rel} is up to date")
            return 0
        print(f"gen_kt: {rel} is out of date; run: python3 tools/protocol-gen/gen_kt.py")
        print(show_diff(text, current))
        return 1

    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text(text, encoding="utf-8", newline="\n")
    print(f"gen_kt: wrote {rel}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
