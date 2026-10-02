"""Export the project's Markdown docs to PDF: one PDF per doc plus one combined, linked PDF.

  python docs/export_pdfs.py            -> docs/pdf/NN-name.pdf and docs/pdf/PixelPine-documentation.pdf

Markdown -> HTML is done here (a small converter for the subset these docs use: headings, paragraphs, lists,
tables, blockquotes, code, links, images), then headless Microsoft Edge / Chrome prints the HTML to PDF.
No extra Python packages needed.
"""
import html
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.parse

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
OUT = os.path.join(HERE, "pdf")

# (source, output name) in reading order; the combined PDF follows the same order
DOCS = [
    ("README.md", "00-project-overview"),
    ("docs/market-analysis.md", "01-market-analysis"),
    ("docs/animations.md", "02-animations-and-firmware-budget"),
    ("docs/power-and-connectivity.md", "03-power-and-connectivity"),
    ("docs/bom.md", "04-bom"),
    ("docs/cost-analysis.md", "05-cost-analysis"),
    ("docs/eu-compliance/README.md", "06-eu-compliance"),
    ("docs/eu-compliance/test-plan.md", "07-emc-test-plan"),
    ("docs/eu-compliance/technical-file.md", "08-technical-file"),
    ("docs/eu-compliance/declaration-of-conformity.md", "09-declaration-of-conformity"),
    ("docs/eu-compliance/labelling-and-instructions.md", "10-labelling-and-instructions"),
    ("firmware/sizetest/README.md", "11-firmware-sizing-prototype"),
    ("docs/superpowers/specs/2026-09-23-pixelpine-design.md", "12-design-spec"),
    ("docs/superpowers/plans/2026-09-23-pixelpine.md", "13-implementation-plan"),
    ("hardware/production/README.md", "14-ordering-at-jlcpcb"),
]
COMBINED = "PixelPine-documentation"
BROWSERS = [r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
            r"C:\Program Files\Microsoft\Edge\Application\msedge.exe",
            r"C:\Program Files\Google\Chrome\Application\chrome.exe"]

CSS = """
@page { size: A4; margin: 17mm 16mm 17mm 16mm;
  @top-right { content: "PixelPine · rev A"; font: 8pt 'Segoe UI', sans-serif; color: #8a968e; }
  @bottom-center { content: counter(page) " / " counter(pages); font: 8pt 'Segoe UI', sans-serif; color: #8a968e; } }
@page cover { @top-right { content: none; } @bottom-center { content: none; } }
html { -webkit-print-color-adjust: exact; print-color-adjust: exact; }
body { font-family: 'Segoe UI', Arial, sans-serif; font-size: 9.6pt; line-height: 1.45; color: #1d2a22; margin: 0; }
h1 { font-size: 19pt; color: #1f5132; border-bottom: 2px solid #1f5132; padding-bottom: 3pt; margin: 0 0 9pt; }
h2 { font-size: 13.5pt; color: #1f5132; margin: 15pt 0 5pt; }
h3 { font-size: 11pt; color: #2d5f41; margin: 12pt 0 4pt; }
h4, h5, h6 { font-size: 10pt; margin: 10pt 0 3pt; }
h1, h2, h3, h4 { break-after: avoid; }
p { margin: 0 0 6pt; }
ul, ol { margin: 0 0 6pt; padding-left: 18pt; }
li { margin: 0 0 2pt; }
li > p { margin: 0 0 2pt; }
table { border-collapse: collapse; width: 100%; margin: 4pt 0 9pt; font-size: 8.5pt; line-height: 1.35; }
th, td { border: 1px solid #c9d3cc; padding: 2.5pt 4.5pt; vertical-align: top; }
th { background: #e3ede6; text-align: left; }
tr { break-inside: avoid; }
tbody tr:nth-child(even) td { background: #f6f9f7; }
code { font-family: Consolas, 'Cascadia Mono', monospace; font-size: 0.9em; background: #eef2ef; padding: 0 2px;
  border-radius: 2px; }
pre { background: #eef2ef; padding: 5pt 8pt; border-radius: 3px; white-space: pre; break-inside: avoid;
  margin: 0 0 8pt; font-family: Consolas, 'Cascadia Mono', monospace; line-height: 1.3; }
pre code { background: none; padding: 0; font-size: 1em; }
blockquote { border-left: 3px solid #d6b13a; margin: 4pt 0 8pt; padding: 3pt 10pt; background: #fbf8ec; }
blockquote p:last-child { margin-bottom: 0; }
hr { border: 0; border-top: 1px solid #c9d3cc; margin: 10pt 0; }
a { color: #1f6f8b; text-decoration: none; }
img { max-width: 100%; }
del { color: #8a968e; }
.doc { break-before: page; }
.src { font-size: 7.5pt; color: #8a968e; margin: 0 0 4pt; }
.sym { font-family: 'Segoe UI Symbol', 'Segoe UI', sans-serif; }
.ok { color: #2e7d32; }
.warn { color: #b26a00; }
.cover { page: cover; text-align: center; padding-top: 18mm; }
.cover h1 { border: 0; font-size: 30pt; margin: 0; }
.cover .sub { font-size: 12pt; color: #2d5f41; margin: 4pt 0 14pt; }
.cover img { width: 62%; border-radius: 6px; }
.cover .meta { font-size: 9pt; color: #5b6a60; margin-top: 10pt; }
.toc { page: cover; break-before: page; }
.toc ol { font-size: 11pt; line-height: 1.9; }
"""


# emoji-presentation characters -> monochrome symbols (Edge embeds a colour font for emoji, which pdf.js and
# some older viewers can't draw)
EMOJI = {"✅": '<span class="sym ok">✔</span>',              # ✅ -> ✔
         "⏳": '<span class="sym warn">◔</span>',            # ⏳ -> ◔ (in progress)
         "⚠": '<span class="sym warn">⚠︎</span>'}      # ⚠ in text presentation


# ------------------------------------------------------------------ Markdown -> HTML
def slug(text):
    """GitHub-style heading anchor."""
    text = re.sub(r"<[^>]+>", "", text)
    text = html.unescape(text).strip().lower()
    text = re.sub(r"[^\w\- ]", "", text)
    return text.replace(" ", "-")


class Converter:
    def __init__(self, link_fn, prefix=""):
        self.link_fn = link_fn          # (url) -> url, rewrites links between docs
        self.prefix = prefix            # heading-id prefix (combined PDF)
        self.title = None

    # ---- inline
    def inline(self, s):
        codes = []

        def keep_code(m):
            codes.append("<code>" + html.escape(m.group(1)) + "</code>")
            return f"\x00{len(codes) - 1}\x00"
        s = re.sub(r"`([^`]+)`", keep_code, s)
        s = html.escape(s, quote=False)
        s = re.sub(r"!\[([^\]]*)\]\(([^)\s]+)\)",
                   lambda m: f'<img alt="{m.group(1)}" src="{self.link_fn(html.unescape(m.group(2)), image=True)}">', s)
        s = re.sub(r"\[([^\]]+)\]\(([^)\s]+)\)",
                   lambda m: f'<a href="{html.escape(self.link_fn(html.unescape(m.group(2))))}">{m.group(1)}</a>', s)
        s = re.sub(r"(?<![\"=/>])(https?://[^\s<)]+)", r'<a href="\1">\1</a>', s)
        s = re.sub(r"\*\*(.+?)\*\*", r"<strong>\1</strong>", s, flags=re.S)       # may span wrapped lines
        s = re.sub(r"(?<![\*\w])\*(?![\s\*])(.+?)(?<![\s\*])\*(?![\*\w])", r"<em>\1</em>", s, flags=re.S)
        s = re.sub(r"(?<![\w])_(?![\s_])(.+?)(?<![\s_])_(?![\w])", r"<em>\1</em>", s, flags=re.S)
        s = re.sub(r"~~(.+?)~~", r"<del>\1</del>", s, flags=re.S)
        s = re.sub(r"(?:  |\\)\n", "<br>\n", s)
        for emoji, sym in EMOJI.items():                # colour-emoji fonts break some PDF viewers
            s = s.replace(emoji, sym)
        return re.sub(r"\x00(\d+)\x00", lambda m: codes[int(m.group(1))], s)

    # ---- blocks
    TABLE_SEP = re.compile(r"^\s*\|?\s*:?-{2,}:?\s*(\|\s*:?-{2,}:?\s*)*\|?\s*$")
    ITEM = re.compile(r"^( *)([-*+]|\d+[.)])\s+(.*)$")

    @staticmethod
    def cells(line):
        line = line.strip()
        if line.startswith("|"):
            line = line[1:]
        if line.endswith("|"):
            line = line[:-1]
        out, cur, code = [], "", False
        for ch in line:
            if ch == "`":
                code = not code
            if ch == "|" and not code:
                out.append(cur.strip())
                cur = ""
            else:
                cur += ch
        out.append(cur.strip())
        return out

    def starts_block(self, lines, i):
        l = lines[i]
        return (l.startswith("```") or re.match(r"^#{1,6}\s", l) or l.startswith(">")
                or re.match(r"^(-{3,}|\*{3,}|_{3,})\s*$", l) or (self.ITEM.match(l) and len(self.ITEM.match(l).group(1)) <= 3)
                or (l.lstrip().startswith("|") and i + 1 < len(lines) and self.TABLE_SEP.match(lines[i + 1])))

    def blocks(self, lines):
        out, i, n = [], 0, len(lines)
        while i < n:
            line = lines[i]
            if not line.strip():
                i += 1
                continue
            if line.startswith("```"):
                j = i + 1
                while j < n and not lines[j].startswith("```"):
                    j += 1
                code = lines[i + 1:j]
                # never wrap (diagrams): shrink the font until the longest line fits the ~480 pt text width
                longest = max((len(l) for l in code), default=1)
                size = min(8.2, 480 / (0.55 * max(longest, 1)))
                out.append(f'<pre style="font-size:{size:.1f}pt"><code>' + html.escape("\n".join(code)) + "</code></pre>")
                i = j + 1
                continue
            m = re.match(r"^(#{1,6})\s+(.*?)\s*#*\s*$", line)
            if m:
                lvl, text = len(m.group(1)), self.inline(m.group(2))
                if lvl == 1 and self.title is None:
                    self.title = html.unescape(re.sub(r"<[^>]+>", "", text))
                out.append(f'<h{lvl} id="{self.prefix}{slug(text)}">{text}</h{lvl}>')
                i += 1
                continue
            if re.match(r"^(-{3,}|\*{3,}|_{3,})\s*$", line):
                out.append("<hr>")
                i += 1
                continue
            if line.lstrip().startswith("|") and i + 1 < n and self.TABLE_SEP.match(lines[i + 1]):
                head = self.cells(line)
                aligns = []
                for c in self.cells(lines[i + 1]):
                    aligns.append("right" if c.endswith(":") and not c.startswith(":") else
                                  "center" if c.startswith(":") and c.endswith(":") else "")
                rows, j = [], i + 2
                while j < n and lines[j].lstrip().startswith("|"):
                    rows.append(self.cells(lines[j]))
                    j += 1

                def td(tag, k, c):
                    a = aligns[k] if k < len(aligns) else ""
                    style = ' style="text-align:%s"' % a if a else ""
                    return f"<{tag}{style}>{self.inline(c)}</{tag}>"
                t = ["<table><thead><tr>" + "".join(td("th", k, c) for k, c in enumerate(head)) + "</tr></thead><tbody>"]
                for r in rows:
                    r = (r + [""] * len(head))[:len(head)]
                    t.append("<tr>" + "".join(td("td", k, c) for k, c in enumerate(r)) + "</tr>")
                out.append("".join(t) + "</tbody></table>")
                i = j
                continue
            if line.startswith(">"):
                j, inner = i, []
                while j < n and lines[j].startswith(">"):
                    inner.append(re.sub(r"^> ?", "", lines[j]))
                    j += 1
                out.append("<blockquote>" + self.blocks(inner) + "</blockquote>")
                i = j
                continue
            m = self.ITEM.match(line)
            if m and len(m.group(1)) <= 3:
                html_list, i = self.list_block(lines, i)
                out.append(html_list)
                continue
            para = [line]
            i += 1
            while i < n and lines[i].strip() and not self.starts_block(lines, i):
                para.append(lines[i])
                i += 1
            out.append("<p>" + self.inline("\n".join(p.strip() if not p.endswith("  ") else p.lstrip() for p in para)) + "</p>")
        return "\n".join(out)

    def list_block(self, lines, i):
        m = self.ITEM.match(lines[i])
        base = len(m.group(1))
        ordered = m.group(2)[0].isdigit()
        start = int(re.match(r"\d+", m.group(2)).group(0)) if ordered else 1
        items, n = [], len(lines)
        while i < n:
            m = self.ITEM.match(lines[i])
            if not (m and len(m.group(1)) == base and m.group(2)[0].isdigit() == ordered):
                break
            content_indent = base + len(m.group(2)) + 1
            body = [m.group(3)]
            i += 1
            while i < n:
                l = lines[i]
                if not l.strip():
                    k = i + 1
                    while k < n and not lines[k].strip():
                        k += 1
                    if k < n and len(lines[k]) - len(lines[k].lstrip()) >= content_indent:
                        body.append("")
                        i += 1
                        continue
                    break
                ind = len(l) - len(l.lstrip())
                mi = self.ITEM.match(l)
                if mi and len(mi.group(1)) <= base:
                    break
                if ind >= content_indent or ind > base:
                    body.append(l[min(ind, content_indent):])
                elif self.starts_block(lines, i):
                    break
                else:
                    body.append(l.strip())              # lazy continuation
                i += 1
            inner = self.blocks(body)
            if inner.count("<p>") == 1 and inner.startswith("<p>"):
                inner = inner.replace("<p>", "", 1).replace("</p>", "", 1)
            items.append("<li>" + inner + "</li>")
        tag = "ol" if ordered else "ul"
        attr = f' start="{start}"' if ordered and start != 1 else ""
        return f"<{tag}{attr}>" + "".join(items) + f"</{tag}>", i

    def convert(self, text):
        return self.blocks(text.replace("\r\n", "\n").split("\n"))


# ------------------------------------------------------------------ paths, links, printing
def winpath(p):
    if shutil.which("cygpath") and not re.match(r"^[A-Za-z]:", p):
        return subprocess.run(["cygpath", "-w", p], capture_output=True, text=True).stdout.strip()
    return p


def file_url(p):
    return "file:///" + urllib.parse.quote(winpath(os.path.abspath(p)).replace("\\", "/"), safe="/:#")


def find_browser():
    for b in BROWSERS:
        cand = b
        if shutil.which("cygpath"):
            cand = subprocess.run(["cygpath", "-u", b], capture_output=True, text=True).stdout.strip()
        if os.path.exists(cand):
            return cand
    sys.exit("No Edge/Chrome found for printing to PDF")


def link_rewriter(src, doc_index, combined):
    src_dir = os.path.dirname(os.path.join(ROOT, src))

    def fn(url, image=False):
        if re.match(r"^[a-z]+:", url) or url.startswith("#"):
            if url.startswith("#") and combined:
                return f"#d{doc_index[src]}-{url[1:]}"
            return url
        path, _, frag = url.partition("#")
        target = os.path.normpath(os.path.join(src_dir, path))
        rel = os.path.relpath(target, ROOT).replace("\\", "/")
        if rel in doc_index and not image:
            k = doc_index[rel]
            if combined:
                return f"#d{k}-{frag}" if frag else f"#d{k}"
            return file_url(os.path.join(OUT, DOCS[k][1] + ".pdf")) + (f"#{frag}" if frag else "")
        return file_url(target) + (f"#{frag}" if frag else "")
    return fn


def page(title, body):
    return (f'<!doctype html><html lang="en"><head><meta charset="utf-8"><title>{html.escape(title)}</title>'
            f"<style>{CSS}</style></head><body>{body}</body></html>")


def pdf_complete(path):
    if not os.path.exists(path) or os.path.getsize(path) < 1000:
        return False
    with open(path, "rb") as f:
        f.seek(-32, os.SEEK_END)
        return b"%%EOF" in f.read()


def print_pdf(browser, html_path, pdf_path, profile):
    """Wait for a complete PDF (it ends with %%EOF), not for the process: since Edge 154 the launcher exits
    at once and a background process writes the file, and sometimes Edge writes it and never exits."""
    env = dict(os.environ, MSYS_NO_PATHCONV="1", MSYS2_ARG_CONV_EXCL="*")   # Git-Bash Python: pass args as-is
    if os.path.exists(pdf_path):
        os.remove(pdf_path)
    p = subprocess.Popen([browser, "--headless=new", "--disable-gpu", "--no-pdf-header-footer",
                          f"--user-data-dir={winpath(profile)}", "--run-all-compositor-stages-before-draw",
                          "--virtual-time-budget=4000", f"--print-to-pdf={winpath(pdf_path)}", file_url(html_path)],
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=env)
    t0 = time.time()
    while time.time() - t0 < 120 and not pdf_complete(pdf_path):
        time.sleep(0.3)
    if p.poll() is None:
        time.sleep(1)                     # let Edge close the file handle before it's stopped
        p.kill()
        p.wait()
    if not pdf_complete(pdf_path):
        sys.exit(f"printing failed for {pdf_path}")


def main():
    os.makedirs(OUT, exist_ok=True)
    browser = find_browser()
    doc_index = {src: k for k, (src, _) in enumerate(DOCS)}
    tmp = tempfile.mkdtemp(prefix="pp_pdf_")
    profile = os.path.join(tmp, "profile")
    parts, toc = [], []
    for k, (src, name) in enumerate(DOCS):
        text = open(os.path.join(ROOT, src), encoding="utf8").read()
        # single PDF
        conv = Converter(link_rewriter(src, doc_index, combined=False))
        body = conv.convert(text)
        title = conv.title or name
        h = os.path.join(tmp, name + ".html")
        open(h, "w", encoding="utf8").write(page(title, f'<div class="doc">{body}</div>'))
        print_pdf(browser, h, os.path.join(OUT, name + ".pdf"), profile)
        # section of the combined PDF
        conv = Converter(link_rewriter(src, doc_index, combined=True), prefix=f"d{k}-")
        body = conv.convert(text)
        parts.append(f'<div class="doc" id="d{k}">{body}</div>')
        toc.append(f'<li><a href="#d{k}">{html.escape(title)}</a> <span class="src">{html.escape(src)}</span></li>')
        print(f"  {name}.pdf  ({title})")
    cover = (f'<div class="cover"><h1>PixelPine</h1><div class="sub">A 7 cm PCB Christmas tree with 240 LEDs and a '
             f'personality. Project documentation, rev A</div>'
             f'<img src="{file_url(os.path.join(ROOT, "hardware", "renders", "pcb_top.png"))}" alt="PixelPine">'
             f'<div class="meta">KTE Project 1 · MachStuff s.r.o. · 2026-09-24</div></div>'
             f'<div class="toc"><h2>Contents</h2><ol>{"".join(toc)}</ol>'
             f'<p class="src">The schematic is a separate PDF: hardware/pixelpine_schematic.pdf.</p></div>')
    body = cover + "".join(parts)
    h = os.path.join(tmp, COMBINED + ".html")
    open(h, "w", encoding="utf8").write(page("PixelPine documentation", body))
    print_pdf(browser, h, os.path.join(OUT, COMBINED + ".pdf"), profile)
    shutil.rmtree(tmp, ignore_errors=True)
    print(f"wrote {len(DOCS)} PDFs + {COMBINED}.pdf to {OUT}")


if __name__ == "__main__":
    main()
