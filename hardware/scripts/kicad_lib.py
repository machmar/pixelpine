"""Tiny helpers for pulling symbol definitions out of KiCad's stock .kicad_sym libraries."""
import os
import re

KICAD_SHARE = os.environ.get("KICAD_SHARE", r"C:/Program Files/KiCad/10.0/share/kicad")
_cache = {}


def _balanced(s, i):
    """Return index just past the s-expression that starts at s[i] == '('."""
    depth = 0
    j = i
    n = len(s)
    while j < n:
        ch = s[j]
        if ch == '"':
            j += 1
            while s[j] != '"':
                if s[j] == "\\":
                    j += 1
                j += 1
        elif ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
            if depth == 0:
                return j + 1
        j += 1
    raise ValueError("unbalanced")


def symbol_block(lib, name):
    """Raw '(symbol "name" ...)' text from symbols/<lib>.kicad_sym (or the project's PixelPine.kicad_sym)."""
    if lib not in _cache:
        path = os.path.join(KICAD_SHARE, "symbols", lib + ".kicad_sym")
        if lib == "PixelPine":
            path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "PixelPine.kicad_sym")
        with open(path, encoding="utf8") as f:
            _cache[lib] = f.read()
    s = _cache[lib]
    m = re.search(r'\(symbol "%s"\s' % re.escape(name), s)
    if not m:
        raise KeyError(f"{lib}:{name}")
    return s[m.start():_balanced(s, m.start())]


def lib_symbol_for_schematic(lib, name):
    """Symbol block renamed to 'lib:name' as required inside a schematic's lib_symbols.
    Sub-unit symbols (name_0_1 ...) keep their short names, as KiCad writes them."""
    blk = symbol_block(lib, name)
    if "(extends " in blk[:400]:
        raise NotImplementedError(f"{lib}:{name} extends another symbol")
    return blk.replace(f'(symbol "{name}"', f'(symbol "{lib}:{name}"', 1)


PIN_RE = re.compile(
    r'\(pin\s+(\w+)\s+\w+\s*\(at\s+([-\d.]+)\s+([-\d.]+)\s+([-\d.]+)\)\s*\(length\s+([\d.]+)\)'
    r'.*?\(name\s+"([^"]*)".*?\(number\s+"([^"]*)"', re.S)


def pins(lib, name):
    """{number: dict(name, x, y, angle, type)} in symbol (Y-up) coordinates; x,y = connection point."""
    out = {}
    for m in PIN_RE.finditer(symbol_block(lib, name)):
        ptype, x, y, ang, length, pname, num = m.groups()
        out[num] = dict(name=pname, x=float(x), y=float(y), angle=float(ang), type=ptype)
    return out
