#!/usr/bin/env python3
"""Check relative Markdown links in the repository.

Ignores http(s), mailto, anchors and fenced code blocks.
Usage: python3 tools/ci/check_links.py
"""

from __future__ import annotations

import re
import sys
from pathlib import Path
from urllib.parse import unquote

ROOT = Path(__file__).resolve().parents[2]
SCAN_DIRS = ("Doc", "App", "Firmware", "Hardware", "protocol", "tools")
LINK_RE = re.compile(r"(?<!!)\[[^\]]*\]\(([^)\s]+)(?:\s+\"[^\"]*\")?\)")
FENCE_RE = re.compile(r"```.*?```", re.DOTALL)


def markdown_files() -> list[Path]:
    files = [ROOT / "README.md"]
    for dirname in SCAN_DIRS:
        base = ROOT / dirname
        if base.exists():
            files.extend(sorted(base.rglob("*.md")))
    return [path for path in files if path.exists()]


def main() -> int:
    errors: list[str] = []
    files = markdown_files()

    for path in files:
        text = FENCE_RE.sub("", path.read_text(encoding="utf-8"))
        for match in LINK_RE.finditer(text):
            target = match.group(1).strip()
            if target.startswith(("http://", "https://", "mailto:", "#")):
                continue
            target = unquote(target.split("#", 1)[0])
            if not target:
                continue
            resolved = (path.parent / target).resolve()
            if not resolved.exists():
                errors.append(f"{path.relative_to(ROOT)} -> {target}")

    if errors:
        print("Link check FAILED:")
        for item in errors:
            print(f"  - {item}")
        return 1

    print(f"Link check OK: {len(files)} Markdown files")
    return 0


if __name__ == "__main__":
    sys.exit(main())
