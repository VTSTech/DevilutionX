#!/usr/bin/env python3
"""Byte-exact patch for preset_parser.cpp (LF+TAB file — do NOT use the Edit
tool on this file; see handoff §9 gotcha).

Fix P1: bare `scale` must not reset a previously declared scale_type back to
Source. RetroArch parses preset keys order-independently, so the declared type
always wins; only the scale VALUE should be filled in.

Idempotent, asserts exactly one match, preserves tabs.
"""

import sys

PATH = "/home/z/my-project/devilutionx-vts/Source/engine/render/gpu/preset/preset_parser.cpp"

OLD = (
    b'\t} else if (key == "scale") {\n'
    b'\t\tpass.scaleX = { ScaleType::Source, ParseFloat(value, 1.0F) };\n'
    b'\t\tpass.scaleY = pass.scaleX;\n'
)

NEW = (
    b'\t} else if (key == "scale") {\n'
    b'\t\t// RetroArch parses preset keys order-independently: a declared\n'
    b'\t\t// scale_type always wins over the bare-scale default. Fill in the\n'
    b'\t\t// scale VALUE only and keep the type this pass already has\n'
    b'\t\t// (ScaleType::Source unless scale_type(_x/_y) preceded).\n'
    b'\t\tpass.scaleX.scale = ParseFloat(value, 1.0F);\n'
    b'\t\tpass.scaleY.scale = pass.scaleX.scale;\n'
)


def main() -> int:
    with open(PATH, "rb") as fh:
        data = fh.read()

    count = data.count(OLD)
    if count == 0 and NEW in data:
        print("already patched; nothing to do")
        return 0
    if count != 1:
        print(f"FAIL: expected exactly 1 match, found {count}")
        return 1

    patched = data.replace(OLD, NEW, 1)
    # Byte-exactness guarantees: only LF endings, tabs preserved elsewhere.
    assert b"\r" not in patched, "CR introduced!"
    with open(PATH, "wb") as fh:
        fh.write(patched)
    print(f"patched {PATH}: {len(data)} -> {len(patched)} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
