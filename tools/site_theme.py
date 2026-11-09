#!/usr/bin/env python3
"""SQ-80's look on its web pages: the boot screen's block-letter wordmark (app/app.c app_splash, the same cells)
and its colours, black, cream and cyan, laid over the pages tools/make_site.py makes (the installer, the front
page, the manual) and the loader page.

    python3 tools/site_theme.py SITE_DIR      every .html page under it, themed in place
"""
import re, sys
from pathlib import Path

CREAM, CYAN, BLACK = "#f2ead6", "#40dce6", "#000000"
GLYPHS = {   # app/app.c LOGO_GLYPH: 6 x 9 cells, a row a number, bit 5 the left cell
    "S": [0x3F, 0x3F, 0x30, 0x3F, 0x3F, 0x03, 0x3F, 0x3F, 0x00],
    "Q": [0x3F, 0x3F, 0x33, 0x33, 0x33, 0x37, 0x3F, 0x3F, 0x03],
    "8": [0x3F, 0x3F, 0x33, 0x3F, 0x3F, 0x33, 0x3F, 0x3F, 0x00],
    "0": [0x3F, 0x3F, 0x33, 0x37, 0x3B, 0x33, 0x3F, 0x3F, 0x00],
}
W, H, GAP = 6, 9, 1.4   # cells; the gap between letters in cells (the boot screen's 7 px of 5)


def logo_svg(height=72, label="SQ-80"):
    """the wordmark as an inline SVG, height px tall (with the rule under it)"""
    rects, x = [], 0.0

    def glyph(g, x0):
        for r, row in enumerate(g):
            c = 0
            while c < W:
                if row >> (W - 1 - c) & 1:
                    e = c
                    while e + 1 < W and row >> (W - 2 - e) & 1:
                        e += 1
                    rects.append(f'<rect x="{x0 + c:g}" y="{r}" width="{e - c + 1}" height="1" fill="{CREAM}"/>')
                    c = e
                c += 1
    glyph(GLYPHS["S"], x); x += W + GAP
    glyph(GLYPHS["Q"], x); x += W + GAP
    q_end = x
    rects.append(f'<rect x="{x:g}" y="3.5" width="3" height="2" fill="{CYAN}"/>')   # the hyphen
    x += 3 + GAP
    glyph(GLYPHS["8"], x); x += W + GAP
    glyph(GLYPHS["0"], x); x += W
    rects.append(f'<rect x="{q_end - GAP + 0.8:g}" y="8.4" width="{x - q_end + GAP - 0.8:g}" height="0.6" fill="{CYAN}"/>')
    vw, vh = x, H + 0.2
    return (f'<svg class="sq80-logo" role="img" aria-label="{label}" viewBox="0 0 {vw:g} {vh:g}" '
            f'height="{height}" width="{height * vw / vh:.0f}" shape-rendering="crispEdges" '
            f'xmlns="http://www.w3.org/2000/svg">{"".join(rects)}</svg>')


CSS = f"""<style id="sq80-theme">
  /* SQ-80's colours (tools/site_theme.py), over the page's own, light and dark alike */
  html:root:root:root {{ --bg: {BLACK}; --fg: {CREAM}; --dim: #9a958a; --accent: {CYAN}; --on-accent: #001215;
    --bad: #ff6b5e; --focus: {CYAN}; --panel: #0d0d0d; --line: #2a2a2a; --hi: {CYAN}; color-scheme: dark; }}
  html body {{ background: {BLACK}; color: {CREAM}; }}
  .sq80-logo {{ display: block; margin: 0 0 6px; max-width: 100%; height: auto; }}
  .sq80-sub {{ color: #9a958a; font-size: 13px; letter-spacing: .14em; text-transform: uppercase; margin: 0 0 28px; }}
  html body h1, html body h2 {{ text-transform: uppercase; letter-spacing: .08em; font-weight: 800; color: {CREAM}; }}
  html body h2 {{ color: {CYAN}; font-size: 14px; }}
  html body a {{ color: {CYAN}; }}
  html body button {{ border-radius: 2px; font-weight: 700; letter-spacing: .06em; text-transform: uppercase;
    background: {CYAN}; color: #001215; }}
  html body button.plain {{ background: none; color: {CREAM}; border: 1px solid #555; }}
  html body button:disabled {{ background: #2a2a2a; color: #777; opacity: 1; }}
  html body progress {{ accent-color: {CYAN}; }}
  html body dialog {{ background: #0d0d0d; color: {CREAM}; border: 1px solid #2a2a2a; border-radius: 4px; }}
  html body section {{ border-radius: 4px; }}
  html body .backup {{ border-color: {CYAN}; border-radius: 4px; }}
  html body .backup strong {{ color: {CYAN}; }}
  html body code {{ background: #161616; color: {CREAM}; }}
</style>
"""


def theme(html, sub, home=False):
    """the page with the theme's style, and the wordmark (and a line under it) in place of its first h1's name"""
    if 'id="sq80-theme"' in html:
        return html
    html = html.replace("</head>", CSS + ('<style>a.home { display: none; }</style>\n' if home else "") + "</head>", 1)
    head = logo_svg() + (f'<p class="sq80-sub">{sub}</p>' if sub else "")
    m = re.search(r"<h1[^>]*>(.*?)</h1>", html, re.S)
    if m:
        inner = m.group(1)
        if re.search(r"SQ-?80", inner):   # "Install SQ80", "SQ-80", "SQ-80 loader": the name becomes the wordmark
            rest = re.sub(r"\s*SQ-?80\s*", " ", inner).strip()
            html = html[:m.start()] + head + (f'<h1>{rest}</h1>' if rest else "") + html[m.end():]
            return html
    return re.sub(r"(<main[^>]*>|<body[^>]*>)", lambda k: k.group(1) + head, html, count=1)


SUBS = {"install": "for the FM-1 · install", "load": "for the FM-1 · your waves and programs",
        "manual": "for the FM-1 · manual"}


def main():
    root = Path(sys.argv[1])
    for p in sorted(root.rglob("*.html")):
        rel = p.relative_to(root).parts
        if rel[0] in ("emu", "words", "firmware"):
            continue
        sub = SUBS.get(rel[0], "for the FM-1") if len(rel) > 1 else "for the FM-1"
        p.write_text(theme(p.read_text(encoding="utf-8"), sub, home=len(rel) == 1), encoding="utf-8")
        print("themed", p.relative_to(root))


if __name__ == "__main__":
    main()
