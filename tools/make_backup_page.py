#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""SQ-80's Backup page: fm1-base's (fm1/base/web/backup/index.html, fm1.toml backup = true) filled in with SQ-80's
own: its banks as the page's own section (app/backup_extras.js), the FM-1's and the SMK-37s' identities, the notes.

    python3 tools/make_backup_page.py SITE_DIR        # writes SITE_DIR/backup/index.html (after make_site.py)
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "fm1" / "base" / "tools"))
from make_site import tool_pages  # noqa: E402

EXTRA = """
  <li>The banks come after the rest, program by program, so a full FM-1 takes a little while. A bank never loaded
    (bank 1: the factory bank) stays so on a restore.</li>
  <li>A backup goes from one unit to another, and between the FM-1 and an SMK-37: an FM-1's banks 3 to 8 have no
    place on an SMK-37, which takes banks 1 and 2. The pattern and the settings go across whole.</li>
  <li>On an SMK-37, <kbd>SEL</kbd> is <b>SCALE</b> and <kbd>HOME</kbd> is <b>PATCH</b>.</li>
  <li>The ROM pack is not in a backup: it is your Ensoniq ROMs, made again from your dumps on the
    <a href="../rompack/">ROM pack page</a>.</li>"""


def main(site):
    values = {
        "NAME": "SQ-80",
        "ID": "FM-1_80|SMK-37 Pro_8|SMK-37 Elite_8|MKE-P37_8",
        "EXTRA": EXTRA,
        "EXTRAS_JS": (ROOT / "app" / "backup_extras.js").read_text(encoding="utf-8"),
        "TERMINAL": "From a terminal, <code>python3 fm1/base/tools/fm1_backup.py backup FILE --magic SQ8B</code> in SQ-80's repository "
                    "backs up the settings and the pattern (not the banks: the Load page and the editor export those).",
    }
    pages = tool_pages(ROOT / "app", values=values)
    out = Path(site) / "backup" / "index.html"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(pages["backup/index.html"], encoding="utf-8")
    print(f"backup   {out}")


if __name__ == "__main__":
    main(sys.argv[1])
