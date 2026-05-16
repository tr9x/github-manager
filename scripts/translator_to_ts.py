#!/usr/bin/env python3
"""Refresh translations/github-manager_pl.ts by walking the C++
source for tr("…") strings and preserving existing translations.

This is a stripped-down replacement for `lupdate` that runs without
Qt's command-line tools installed. The real `lupdate` is the
canonical solution — invoke it via:

    cmake --build build --target github-manager_lupdate

This script is a fallback for environments without lupdate (Docker
images, CI runners without qt6-tools), and a reference for the
0.9.0 migration that seeded the .ts from the legacy hardcoded
hashmap.

What it does:
  1. Walks src/ for every tr("…") string
  2. Reads the existing translations/github-manager_pl.ts to get
     translations for strings we already have
  3. Writes a new .ts where every code string has an entry, with
     translation pulled from the legacy data when available,
     marked type="unfinished" when not

Limitations vs. real lupdate:
  * Single context "ghm" for everything (lupdate groups by class)
  * No source location info (line numbers etc.)
  * No plurals
  * Only matches the simple tr("…") form

Run from project root:
    python3 scripts/translator_to_ts.py
"""

from __future__ import annotations

import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


SRC_DIR = Path("src")
TS_FILE = Path("translations/github-manager_pl.ts")

LANG_CODE = "pl_PL"
SOURCE_LANG = "en_US"


# Match tr("…") with one or more concatenated string literals as arg.
TR_CALL_RE = re.compile(
    r"\btr\(\s*"
    r"((?:\"(?:[^\"\\]|\\.)*\"\s*)+)"
    r"\s*(?:,|\))",
    re.DOTALL,
)

PIECE_RE = re.compile(r'"((?:[^"\\]|\\.)*)"')

C_ESCAPES = {
    "n": "\n", "t": "\t", "r": "\r",
    "\\": "\\", '"': '"', "'": "'", "0": "\0",
}


def unescape(s: str) -> str:
    out = []
    i = 0
    while i < len(s):
        ch = s[i]
        if ch == "\\" and i + 1 < len(s):
            nxt = s[i + 1]
            out.append(C_ESCAPES.get(nxt, nxt))
            i += 2
        else:
            out.append(ch)
            i += 1
    return "".join(out)


def join_pieces(blob: str) -> str:
    return "".join(unescape(m.group(1)) for m in PIECE_RE.finditer(blob))


def extract_code_strings() -> list[str]:
    """Walk src/ and return every unique tr() string in discovery order."""
    seen: set[str] = set()
    order: list[str] = []
    files = sorted(SRC_DIR.rglob("*.cpp")) + sorted(SRC_DIR.rglob("*.h"))
    for path in files:
        text = path.read_text(encoding="utf-8")
        text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
        text = re.sub(r"//[^\n]*", "", text)
        for m in TR_CALL_RE.finditer(text):
            s = join_pieces(m.group(1))
            if s and s not in seen:
                seen.add(s)
                order.append(s)
    return order


def load_existing_translations() -> dict[str, str]:
    """Load source→translation from the existing .ts file. Returns
    empty dict if the file doesn't exist yet (first run).
    """
    if not TS_FILE.exists():
        return {}
    tree = ET.parse(TS_FILE)
    out: dict[str, str] = {}
    for msg in tree.iter("message"):
        src = msg.find("source")
        tr = msg.find("translation")
        if src is None or src.text is None:
            continue
        if tr is None or tr.text is None or not tr.text.strip():
            continue
        if tr.get("type") == "unfinished":
            continue
        out[src.text] = tr.text
    return out


def xml_escape(s: str) -> str:
    return (s.replace("&", "&amp;")
             .replace("<", "&lt;")
             .replace(">", "&gt;"))


def main() -> int:
    if not SRC_DIR.exists():
        print("error: must run from project root (no src/)", file=sys.stderr)
        return 1

    code_strings = extract_code_strings()
    existing = load_existing_translations()
    print(f"# Code strings:          {len(code_strings)}", file=sys.stderr)
    print(f"# Existing translations: {len(existing)}", file=sys.stderr)

    translated = 0
    untranslated = 0
    final: list[tuple[str, str]] = []
    for src in code_strings:
        if src in existing:
            final.append((src, existing[src]))
            translated += 1
        else:
            final.append((src, ""))
            untranslated += 1
    print(f"# Translated:            {translated}", file=sys.stderr)
    print(f"# New (unfinished):      {untranslated}", file=sys.stderr)

    # Write the .ts file. We construct the XML by hand instead of
    # using minidom.toprettyxml — that method strips blank lines
    # during pretty-print, which corrupts source strings containing
    # \n\n into \n. Discovered the hard way during 0.9.0.

    lines = [
        '<?xml version="1.0" encoding="utf-8"?>',
        '<!DOCTYPE TS>',
        f'<TS version="2.1" sourcelanguage="{SOURCE_LANG}" language="{LANG_CODE}">',
        '<context>',
        '    <name>ghm</name>',
    ]
    for src, tr in final:
        lines.append('    <message>')
        lines.append(f'        <source>{xml_escape(src)}</source>')
        if tr:
            lines.append(f'        <translation>{xml_escape(tr)}</translation>')
        else:
            lines.append('        <translation type="unfinished"></translation>')
        lines.append('    </message>')
    lines.append('</context>')
    lines.append('</TS>')

    TS_FILE.parent.mkdir(parents=True, exist_ok=True)
    TS_FILE.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"# Wrote {TS_FILE} ({TS_FILE.stat().st_size} bytes).",
          file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
