#!/usr/bin/env python3
"""Expand the single-context translation file produced by
translator_to_ts.py into a multi-context file matching what real
lupdate produces.

Problem this solves: legacy translator_to_ts.py emitted all 375
translation entries under one synthetic <name>ghm</name> context.
QTranslator::translate() looks up strings keyed on (context, source),
where context is the C++ class name passed by Qt's tr() macro (the
class containing the Q_OBJECT macro). So a tr("Refresh") in
MainWindow looks up ("MainWindow", "Refresh"), and our single
"ghm"-context .ts had nothing for it — translation silently failed.

Fix: walk the C++ sources, find every class with Q_OBJECT, then
duplicate every "ghm"-context message into every such context.
This is the same end-state real lupdate would arrive at over time
(though lupdate is more precise — it only puts a string into the
context where it actually appears). The redundancy costs us
~22× more .qm bytes but produces a correctly-functioning
translation file in one step.

Idempotent: re-running on an already-multi-context .ts leaves it
unchanged (contexts that already exist aren't re-seeded).

Usage:
    python3 scripts/expand_ts_contexts.py
        # → rewrites translations/github-manager_pl.ts in place
"""

from __future__ import annotations

import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path
from xml.dom import minidom


SRC_ROOT = Path("src")
TS_PATH = Path("translations/github-manager_pl.ts")


# Match `class Foo : ... { ... Q_OBJECT ... }`. We extract Foo when
# we see Q_OBJECT inside the class body. Simpler than parsing C++:
# find class declarations, then check whether each contains Q_OBJECT
# before the next class.
CLASS_RE = re.compile(
    r"\bclass\s+([A-Z][A-Za-z0-9_]*)"  # class Foo
    r"(?:\s+final)?"
    r"(?:\s*:\s*[^{;]+)?"              # optional inheritance list
    r"\s*\{",                          # opening brace
)

# Match `namespace ghm::ui {` or `namespace foo {` to track which
# namespace a class lives in. Qt's tr() uses the fully-qualified
# class name as the lookup context (e.g. "ghm::ui::MainWindow"),
# so we need namespace-aware context tagging.
NAMESPACE_RE = re.compile(
    r"\bnamespace\s+([A-Za-z_][A-Za-z0-9_:]*)\s*\{"
)


def q_object_contexts_in(path: Path) -> set[str]:
    """Return the set of fully-qualified Q_OBJECT class names in
    `path`. "Fully qualified" means including the namespace path,
    e.g. "ghm::ui::MainWindow", because that's what Qt's
    QObject::tr() uses as the lookup context for namespace'd
    classes (moc generates className() returning the qualified
    name)."""
    text = path.read_text(encoding="utf-8", errors="replace")
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    text = re.sub(r"//[^\n]*", "", text)

    # Find the position of every namespace open and every class
    # declaration; walk them in order to track the active namespace
    # stack. This is a simplified C++ parse — it doesn't handle
    # nested classes, anonymous namespaces, or namespace alias
    # syntax. Good enough for this codebase.
    events: list[tuple[int, str, str]] = []  # (pos, kind, name)
    for m in NAMESPACE_RE.finditer(text):
        events.append((m.start(), "ns_open", m.group(1)))
    for m in CLASS_RE.finditer(text):
        body_start = m.end()
        # Find matching close brace by tracking depth from body_start.
        depth = 1
        i = body_start
        while i < len(text) and depth > 0:
            ch = text[i]
            if ch == "{": depth += 1
            elif ch == "}": depth -= 1
            i += 1
        body_end = i
        body = text[body_start:body_end]
        if "Q_OBJECT" in body:
            events.append((m.start(), "qclass", m.group(1)))

    # Sort by position; we don't actually need to track all opens and
    # closes because in this codebase every Q_OBJECT class is directly
    # inside exactly one `namespace ghm::xxx { ... }` block. So a
    # simpler rule works: the namespace containing a class is the
    # last namespace declared before the class.
    events.sort()
    contexts: set[str] = set()
    current_ns: str | None = None
    for _pos, kind, name in events:
        if kind == "ns_open":
            current_ns = name
        elif kind == "qclass":
            if current_ns:
                contexts.add(f"{current_ns}::{name}")
            else:
                contexts.add(name)
    return contexts


def collect_q_object_classes() -> set[str]:
    contexts: set[str] = set()
    for ext in ("*.h", "*.cpp"):
        for path in SRC_ROOT.rglob(ext):
            contexts |= q_object_contexts_in(path)
    return contexts


def main() -> int:
    if not TS_PATH.exists():
        print(f"error: {TS_PATH} not found — run from project root", file=sys.stderr)
        return 1

    classes = collect_q_object_classes()
    if not classes:
        print("error: no Q_OBJECT classes found — check SRC_ROOT", file=sys.stderr)
        return 1
    print(f"Found {len(classes)} Q_OBJECT classes: "
          f"{', '.join(sorted(classes))}", file=sys.stderr)

    tree = ET.parse(TS_PATH)
    root = tree.getroot()

    # Find the "ghm" context (legacy seed). If missing, assume the
    # file is already multi-context and bail with success.
    ghm_ctx = None
    for ctx in root.findall("context"):
        name_el = ctx.find("name")
        if name_el is not None and name_el.text == "ghm":
            ghm_ctx = ctx
            break

    if ghm_ctx is None:
        print("No 'ghm' synthetic context found — assuming already migrated, "
              "nothing to do.", file=sys.stderr)
        return 0

    # Gather existing context names so we don't overwrite hand-curated
    # per-class translations that may have been added since the seed.
    existing_contexts = {
        c.find("name").text
        for c in root.findall("context")
        if c.find("name") is not None
    }

    # Build a list of (source, translation) pairs from the ghm context.
    pairs: list[tuple[str, str]] = []
    for msg in ghm_ctx.findall("message"):
        src_el = msg.find("source")
        trn_el = msg.find("translation")
        if src_el is None or src_el.text is None:
            continue
        translation = (trn_el.text or "") if trn_el is not None else ""
        pairs.append((src_el.text, translation))

    print(f"Cloning {len(pairs)} entries into each Q_OBJECT context…",
          file=sys.stderr)

    # For each Q_OBJECT class not already present, append a context
    # with all the pairs.
    for cls in sorted(classes):
        if cls in existing_contexts:
            continue
        ctx_el = ET.SubElement(root, "context")
        name_el = ET.SubElement(ctx_el, "name")
        name_el.text = cls
        for source, target in pairs:
            msg_el = ET.SubElement(ctx_el, "message")
            ET.SubElement(msg_el, "source").text = source
            t_el = ET.SubElement(msg_el, "translation")
            t_el.text = target

    # Remove the synthetic "ghm" context — it's been distributed.
    root.remove(ghm_ctx)

    # Pretty-print and write. Using minidom for indentation; the
    # tradeoff is that it normalises whitespace in text nodes, which
    # would mangle our multi-line source strings. To preserve them we
    # serialise without minidom and accept ugly XML — Qt Linguist
    # reads it fine either way.
    #
    # We DO want a stable file though, so we put each <message> on
    # multiple lines manually.
    write_ts_preserving_newlines(tree, TS_PATH)

    print(f"Wrote {TS_PATH} with {len(classes)} class contexts.",
          file=sys.stderr)
    return 0


def write_ts_preserving_newlines(tree: ET.ElementTree, path: Path) -> None:
    """Serialise the .ts ElementTree without minidom (which collapses
    multi-line text into single lines, breaking source strings that
    contain real newlines)."""
    root = tree.getroot()

    lines: list[str] = []
    lines.append('<?xml version="1.0" encoding="utf-8"?>')
    lines.append('<!DOCTYPE TS>')
    attrs = " ".join(f'{k}="{v}"' for k, v in root.attrib.items())
    lines.append(f"<TS {attrs}>" if attrs else "<TS>")

    for ctx in root.findall("context"):
        lines.append("    <context>")
        nm = ctx.find("name")
        if nm is not None and nm.text:
            lines.append(f"        <name>{escape_xml(nm.text)}</name>")
        for msg in ctx.findall("message"):
            src = msg.find("source")
            trn = msg.find("translation")
            src_text = escape_xml(src.text or "") if src is not None else ""
            trn_text = escape_xml(trn.text or "") if trn is not None else ""
            lines.append("        <message>")
            lines.append(f"            <source>{src_text}</source>")
            lines.append(f"            <translation>{trn_text}</translation>")
            lines.append("        </message>")
        lines.append("    </context>")
    lines.append("</TS>")
    lines.append("")

    path.write_text("\n".join(lines), encoding="utf-8")


def escape_xml(s: str) -> str:
    return (s.replace("&", "&amp;")
             .replace("<", "&lt;")
             .replace(">", "&gt;"))


if __name__ == "__main__":
    sys.exit(main())
