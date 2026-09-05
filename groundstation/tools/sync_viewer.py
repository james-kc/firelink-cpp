#!/usr/bin/env python3
"""Sync the ground-station flight viewer from the embedded flight-computer page.

The viewer lives verbatim as one C++ raw-string literal in
src/web/flight_page.cpp (the Pi serves it with zero external files) and is
mirrored here for the Python ground station. This script extracts that string
and rewrites groundstation/firelink_gs/_flight_viewer.html so the two can't
drift. Run it after editing the viewer.

Usage: python tools/sync_viewer.py
"""

import pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
SRC = ROOT / "src" / "web" / "flight_page.cpp"
DST = ROOT / "groundstation" / "firelink_gs" / "_flight_viewer.html"

OPEN = 'R"FP('
CLOSE = ')FP";'


def extract(text):
    start = text.index(OPEN) + len(OPEN)
    end = text.index(CLOSE, start)
    return text[start:end]


def main():
    if not SRC.exists():
        raise SystemExit(f"not found: {SRC}")
    html = extract(SRC.read_text(encoding="utf-8"))
    marker = (
        "<!-- GENERATED from src/web/flight_page.cpp by "
        "groundstation/tools/sync_viewer.py - do not edit by hand. -->\n"
    )
    DST.write_text(marker + html, encoding="utf-8")
    print(f"wrote {DST} ({len(html)} chars)")


if __name__ == "__main__":
    main()