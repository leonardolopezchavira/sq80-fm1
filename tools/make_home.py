#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""The site's front page from app/site/home.html, in place of the one tools/make_site.py makes from landing.md:
{{VERSION}} and {{PKG}} (the package's file name in firmware/) filled in; and SQ-80's eight banks (app/site/banks)
into SITE_DIR/banks, where the installer and the emulator find them.

    python3 tools/make_home.py SITE_DIR VERSION
"""
import sys
from pathlib import Path

root, version = Path(sys.argv[1]), sys.argv[2]
pkg = sorted((root / "firmware").glob(f"*-{version}.fwsc"))
html = (Path(__file__).resolve().parents[1] / "app" / "site" / "home.html").read_text(encoding="utf-8")
html = html.replace("{{VERSION}}", version).replace("{{PKG}}", pkg[0].name if pkg else "")
(root / "index.html").write_text(html, encoding="utf-8")
banks = Path(__file__).resolve().parents[1] / "app" / "site" / "banks"   # SQ-80's eight banks: the installer and emulator
(root / "banks").mkdir(exist_ok=True)
for f in sorted(banks.glob("*.syx")):
    (root / "banks" / f.name).write_bytes(f.read_bytes())
print(f"home     {root / 'index.html'} ({version}, {pkg[0].name if pkg else 'no package'})")
