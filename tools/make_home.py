#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""The site's front page from app/site/home.html, in place of the one tools/make_site.py makes from landing.md:
{{VERSION}} and {{PKG}} (the package's file name in firmware/) filled in.

    python3 tools/make_home.py SITE_DIR VERSION
"""
import sys
from pathlib import Path

root, version = Path(sys.argv[1]), sys.argv[2]
pkg = sorted((root / "firmware").glob(f"*-{version}.fwsc"))
html = (Path(__file__).resolve().parents[1] / "app" / "site" / "home.html").read_text(encoding="utf-8")
html = html.replace("{{VERSION}}", version).replace("{{PKG}}", pkg[0].name if pkg else "")
(root / "index.html").write_text(html, encoding="utf-8")
print(f"home     {root / 'index.html'} ({version}, {pkg[0].name if pkg else 'no package'})")
