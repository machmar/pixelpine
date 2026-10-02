"""Generate hardware/pixelpine.kicad_sch (KiCad 10) from geometry.py.

Blocks are drawn with real wires (MCU -> line resistors, USB-C -> CC resistors / bulk cap, LED rows on an
anode bus); net labels only carry signals *between* blocks (L0..L15 lines into the LED matrix).
"""
import os
import uuid

import geometry as g
import kicad_lib as kl

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "pixelpine.kicad_sch")
PROJECT = "pixelpine"
NS = uuid.UUID("6f1e3c2a-1d2b-4c5d-9e8f-000000000001")
ROOT_UUID = str(uuid.uuid5(NS, "root"))


def uid(*key):
    return str(uuid.uuid5(NS, "/".join(map(str, key))))


def q(s):
    return '"' + str(s).replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'


def f(v):
    return f"{v:.4f}".rstrip("0").rstrip(".")


def eff(size=1.27, hide=False, left=False):
    h = "(hide yes) " if hide else ""
    j = " (justify left)" if left else ""
    return f"{h}(effects (font (size {size} {size})){j})"


# ---- parts: lib, symbol, footprint, value, LCSC ----
PARTS = {
    "MCU": ("PixelPine", "PY32F003F18P6", "Package_SO:TSSOP-20_4.4x6.5mm_P0.65mm", "PY32F003F18P6", "C55338319"),
    "LED": ("Device", "LED", "PixelPine:LED_0603_Dense", "XL-1608WWC-06", "C965810"),
    "R100": ("Device", "R", "Resistor_SMD:R_0402_1005Metric", "100", "C25076"),
    "R5K1": ("Device", "R", "Resistor_SMD:R_0402_1005Metric", "5.1k", "C25905"),
    "R1K": ("Device", "R", "Resistor_SMD:R_0402_1005Metric", "1k", "C11702"),
    "C10U": ("Device", "C", "Capacitor_SMD:C_0603_1608Metric", "10u", "C19702"),
    "C100N": ("Device", "C", "Capacitor_SMD:C_0402_1005Metric", "100n", "C1525"),
    "USB": ("Connector", "USB_C_Receptacle_PowerOnly_6P",
            "PixelPine:USB_C_SHOUHAN_TYPE-C-6P", "TYPE-C 6P", "C456012"),
    "TP": ("Connector", "TestPoint", "TestPoint:TestPoint_Pad_D1.5mm", "TP", ""),
}

items = []          # schematic body s-exprs
used_libs = set()
pwr_count = [0]


def xform(px, py, rot):
    """Library pin (Y up) -> sheet offset (Y down) for a symbol rotated `rot` degrees (CCW on screen)."""
    vx, vy = px, -py
    for _ in range((rot // 90) % 4):
        vx, vy = vy, -vx
    return vx, vy


def symbol(kind, ref, x, y, rot=0, value=None, fp=None, lcsc=None, ref_at=None, val_at=None, hide_value=False,
           lib=None, name=None, in_bom=True, on_board=True, left_text=False, fsize=1.27):
    if kind in PARTS:
        lib, name, dfp, dval, dlcsc = PARTS[kind]
        fp = fp if fp is not None else dfp
        value = value if value is not None else dval
        lcsc = lcsc if lcsc is not None else dlcsc
    used_libs.add((lib, name))
    rx, ry = ref_at if ref_at else (x, y - 5.08)
    vx, vy = val_at if val_at else (x, y + 5.08)
    fa = 90 if rot in (90, 270) else 0        # KiCad field angles are relative: 90 on a 90-deg symbol = horizontal
    pins = kl.pins(lib, name)
    props = [
        f'(property "Reference" {q(ref)} (at {f(rx)} {f(ry)} {fa}) {eff(fsize, ref.startswith("#"), left_text)})',
        f'(property "Value" {q(value)} (at {f(vx)} {f(vy)} {fa}) {eff(fsize, hide_value, left_text)})',
        f'(property "Footprint" {q(fp or "")} (at {f(x)} {f(y)} 0) {eff(hide=True)})',
        f'(property "Datasheet" "" (at {f(x)} {f(y)} 0) {eff(hide=True)})',
        f'(property "Description" "" (at {f(x)} {f(y)} 0) {eff(hide=True)})',
    ]
    if lcsc:
        props.append(f'(property "LCSC" {q(lcsc)} (at {f(x)} {f(y)} 0) {eff(hide=True)})')
    pin_s = " ".join(f'(pin {q(n)} (uuid "{uid(ref, "pin", n)}"))' for n in sorted(pins))
    items.append(
        f'(symbol (lib_id "{lib}:{name}") (at {f(x)} {f(y)} {rot}) (unit 1) (exclude_from_sim no) '
        f'(in_bom {"yes" if in_bom else "no"}) (on_board {"yes" if on_board else "no"}) (dnp no) '
        f'(uuid "{uid(ref)}") {" ".join(props)} {pin_s} '
        f'(instances (project "{PROJECT}" (path "/{ROOT_UUID}" (reference {q(ref)}) (unit 1)))))')
    out = {}
    for n, p in pins.items():
        ox, oy = xform(p["x"], p["y"], rot)
        out[n] = (round(x + ox, 4), round(y + oy, 4))
    return out


def label(name, x, y, right=True):
    ang, just = (0, "left bottom") if right else (180, "right bottom")
    items.append(f'(label {q(name)} (at {f(x)} {f(y)} {ang}) '
                 f'(effects (font (size 1.27 1.27)) (justify {just})) (uuid "{uid("lbl", name, x, y)}"))')


def power(net, x, y):
    pwr_count[0] += 1
    ref = f"#PWR{pwr_count[0]:03d}" if net != "PWR_FLAG" else f"#FLG{pwr_count[0]:03d}"
    val_off = 3.81 if net == "GND" else -3.81
    symbol(None, ref, x, y, value=net, fp="", lcsc="", lib="power", name=net,
           ref_at=(x, y), val_at=(x, y + val_off), in_bom=False, on_board=False)


def wire(*pts):
    """Polyline of wire segments through the given (x, y) points."""
    for (x1, y1), (x2, y2) in zip(pts, pts[1:]):
        items.append(f'(wire (pts (xy {f(x1)} {f(y1)}) (xy {f(x2)} {f(y2)})) '
                     f'(stroke (width 0) (type default)) (uuid "{uid("w", x1, y1, x2, y2)}"))')


def junction(x, y):
    items.append(f'(junction (at {f(x)} {f(y)}) (diameter 0) (color 0 0 0 0) (uuid "{uid("j", x, y)}"))')


def text(s, x, y, size=1.27, bold=False):
    b = " (bold yes)" if bold else ""
    items.append(f'(text {q(s)} (exclude_from_sim no) (at {f(x)} {f(y)} 0) '
                 f'(effects (font (size {size} {size}){b}) (justify left top)) (uuid "{uid("txt", s[:40], x, y)}"))')


def rect(x1, y1, x2, y2):
    items.append(f'(rectangle (start {f(x1)} {f(y1)}) (end {f(x2)} {f(y2)}) '
                 f'(stroke (width 0.254) (type dash)) (fill (type none)) (uuid "{uid("rect", x1, y1)}"))')


def testpoint(ref, x, y, value, fp="TestPoint:TestPoint_Pad_D1.5mm", rot=270):
    """Test pad whose pin sits at (x, y); rot 270 = body to the right, 90 = left, 0 = up, 180 = down."""
    tx, ty, jl = {270: (x + 5.715, y - 0.635, True), 90: (x - 12.7, y - 0.635, True),
                  0: (x + 1.905, y - 4.445, True), 180: (x + 2.54, y + 3.175, False)}[rot]
    # (KiCad mirrors field justification on a 180° symbol, so "right" renders left-justified there)
    return symbol("TP", ref, x, y, rot=rot, value=value, fp=fp, lcsc="", in_bom=False,
                  ref_at=(tx, ty), val_at=(tx, ty + 2.54), left_text=jl)


# ============================ block 1: MCU + line resistors + touch + programming ============================
text("MCU  —  PY32F003F18P6 + 16 line resistors + touch star", 20.32, 22.86, 2.0, True)
rect(17.78, 20.32, 152.4, 124.46)
UX, UY = 95.25, 76.2
MCU_LIB, MCU_SYM = PARTS["MCU"][0], PARTS["MCU"][1]
upins = symbol("MCU", "U1", UX, UY, ref_at=(UX + 2.54, UY - 19.05), val_at=(UX - 25.4, UY + 17.78), left_text=True)
lib_pins = kl.pins(MCU_LIB, MCU_SYM)
line_of = {str(pin): i for i, (_, pin) in enumerate(g.LINE_PINS)}
LBL_L, LBL_R = UX - 39.37, UX + 39.37          # label columns left / right of the resistor fan
for num, (px, py) in upins.items():
    pname = lib_pins[num]["name"]
    left = px < UX
    sgn = -1 if left else 1
    stagger = int(round(py / 2.54)) % 2          # alternate near / far so reference texts never collide
    rc = px + sgn * (7.62 if stagger == 0 else 17.78)     # resistor centre
    rot = 270 if left else 90                    # pin 1 always faces the MCU
    lbl_x = LBL_L if left else LBL_R
    if pname == "VCC":
        top = (px, py - 7.62)
        wire((px, py), top, (px, py - 10.16))
        power("+5V", px, py - 10.16)
        wire(top, (px - 15.24, py - 7.62))       # decoupling cap on the left, away from the SWD pad
        junction(*top)
        c2 = symbol("C100N", "C2", px - 15.24, py - 3.81, ref_at=(px - 12.7, py - 5.08),
                    val_at=(px - 12.7, py - 2.54), left_text=True)
        power("GND", *c2["2"])
        continue
    if pname == "VSS":
        wire((px, py), (px, py + 2.54))
        power("GND", px, py + 2.54)
        continue
    if num in line_of:
        i = line_of[num]
        r = symbol("R100", f"R{i + 1}", rc, py, rot=rot, ref_at=(rc - 1.905, py - 1.524),
                   val_at=(rc + 1.905, py - 1.524), fsize=1.0)
        if pname.endswith(("SWDIO", "SWCLK")):   # programming pad on the MCU side of the line resistor
            tap = (px + sgn * 2.54, py)
            wire((px, py), tap, r["1"])
            junction(*tap)
            dy = -5.08 if pname.endswith("SWDIO") else 5.08   # lift the pad clear of the resistor texts
            wire(tap, (tap[0], py + dy))
            if pname.endswith("SWDIO"):
                testpoint("TP3", tap[0], py + dy, "SWDIO", rot=0)
            else:
                testpoint("TP5", tap[0], py + dy, "SWCLK", rot=180)
        else:
            wire((px, py), r["1"])
        wire(r["2"], (lbl_x, py))
        label(f"L{i}", lbl_x, py, right=not left)
    elif str(num) == str(g.TOUCH_PIN[1]):        # capacitive touch: ESD resistor, then the copper star
        r = symbol("R1K", "R19", rc, py, rot=rot, ref_at=(rc - 1.905, py - 1.524),
                   val_at=(rc + 1.905, py - 1.524), fsize=1.0)
        wire((px, py), r["1"])
        wire(r["2"], (lbl_x, py))
        testpoint("TP4", lbl_x, py, "STAR", fp="PixelPine:TouchStar", rot=90 if left else 270)
    elif str(num) == str(g.NRST_PIN[1]):         # reset kept for connect-under-reset programming
        wire((px, py), (lbl_x, py))
        testpoint("TP6", lbl_x, py, "NRST")
text("R1–R16: 100 Ω per charlieplex line (keeps the common anode pin < 20 mA).\n"
     "PA7 = ADC: charge-sharing capacitive touch on the star (hover / touch / press / squeeze).\n"
     "R19 1 kΩ: ESD protection for the exposed star (EN 61547).\n"
     "SWD pins PA13/PA14 double as lines L8/L9: program with connect-under-reset (TP3, TP5, TP6).\n"
     "PF4/BOOT0 = line L15: its reset pull-down keeps boot-from-flash; firmware then floats it.",
     20.32, 104.14, 1.27)

# ============================ block 2: USB-C power ============================
text("POWER  —  USB-C 6-pin, power only, 5 V sink", 157.48, 22.86, 2.0, True)
rect(154.94, 20.32, 264.16, 124.46)
JX, JY = 182.88, 76.2
jp = symbol("USB", "J1", JX, JY, ref_at=(JX, JY - 21.59), val_at=(JX - 5.08, JY + 27.94))
vb = jp["A9"]
flag = (vb[0] + 5.08, vb[1])
node = (vb[0] + 10.16, vb[1])
c1x = vb[0] + 17.78
wire(vb, flag, node, (c1x, vb[1]), (c1x + 10.16, vb[1]))
power("PWR_FLAG", *flag)
wire(node, (node[0], node[1] - 5.08))
power("+5V", node[0], node[1] - 5.08)
junction(*node)
junction(c1x, vb[1])
c1 = symbol("C10U", "C1", c1x, vb[1] + 3.81, ref_at=(c1x + 2.54, vb[1] + 2.54), val_at=(c1x + 2.54, vb[1] + 5.08),
            left_text=True)
power("GND", *c1["2"])
testpoint("TP1", c1x + 10.16, vb[1], "+5V")
# CC pull-downs (Rd = 5.1 k) -> USB-C sources enable VBUS
gx = vb[0] + 27.94
for ref, pin, net, off in (("R17", "A5", "CC1", 11.43), ("R18", "B5", "CC2", 19.05)):
    px, py = jp[pin]
    ty = py - 1.524 if ref == "R17" else py + 2.286
    r = symbol("R5K1", ref, px + off, py, rot=90, ref_at=(px + off - 1.905, ty), val_at=(px + off + 1.905, ty),
               fsize=1.0)
    wire((px, py), (px + 1.27, py), r["1"])
    label(net, px + 1.27, py)
    wire(r["2"], (gx, py))
cc1y, cc2y = jp["A5"][1], jp["B5"][1]
wire((gx, cc1y), (gx, cc2y), (gx, cc2y + 5.08))
junction(gx, cc2y)
power("GND", gx, cc2y + 5.08)
# ground + shield
gp, sh = jp["A12"], jp["SH"]
gnd = (gp[0], gp[1] + 2.54)
wire(gp, gnd)
wire(sh, (sh[0], gnd[1]), gnd)
wire(gnd, (gnd[0] + 5.08, gnd[1]), (gnd[0] + 10.16, gnd[1]))
power("PWR_FLAG", gnd[0] + 5.08, gnd[1])
junction(*gnd)
power("GND", *gnd)
testpoint("TP2", gnd[0] + 10.16, gnd[1], "GND")
text("No fuse / TVS / switch: cost-down decision.\nTypical draw ≈ 10 mA, max 21 mA @ 5 V.\n"
     "TP1–TP3, TP5, TP6: bare pads on the back for the pogo-pin SWD jig.", 157.48, 106.68, 1.27)

# ============================ block 3: notes ============================
NOTES = (
    "DRIVE SCHEME\n"
    "Scan by anode: phase p (0..15) drives line Lp HIGH, the cathodes of LEDs lit in that phase LOW,\n"
    "all other lines Hi-Z. 16 phases per frame, binary-code-modulated PWM (8 bit, 2 µs slot, 122 Hz).\n"
    "LED Dn+1 sits at board position n (reading order, top->bottom, left->right). Which anode/cathode pair sits at\n"
    "which position is chosen by scripts/optimize_leds.py (hardware/led_assignment.json): neighbouring LED pads\n"
    "share line nets, so each net is a few copper islands joined by tiny bridges and the board routes on 2 layers.\n\n"
    "TOUCH STAR\n"
    "The star is one electrode on BOTH copper layers, joined by a plated hole. PA7 samples it with\n"
    "ADC charge sharing: hover < light touch < firm press < squeeze (pinched front + back = biggest signal).\n\n"
    "PRODUCTION\n"
    "PY32F003F18P6 = TSSOP-20 'pinout 1' (datasheet fig. 3-3). SWD via pogo pads TP3 (SWDIO), TP5 (SWCLK),\n"
    "TP6 (NRST), connect-under-reset; after boot the firmware takes PA13/PA14 over as lines L8/L9."
)
text(NOTES, 269.24, 22.86, 1.27)
rect(266.7, 20.32, 406.4, 124.46)

# ============================ block 4: LED matrix ============================
MX0, MY0 = 30.48, 142.24          # first LED of the matrix
MDX, MDY = 16.51, 8.89             # column / row pitch (dense layout)
text("LED MATRIX  —  240 × XL-1608WWC-06 warm white 0603, charlieplexed on 16 lines", 20.32, 129.54, 2.0, True)
rect(17.78, 127.0, MX0 + 14 * MDX + 10.16, MY0 + 15 * MDY + 6.35)
text("HOW TO READ THE MATRIX\n\n"
     "Each row is one charlieplex line driving the ANODES\n"
     "(bus wire, line name on the left end of the bus).\n\n"
     "Each LED's CATHODE goes to the line named\n"
     "just left of it.\n\n"
     "Reference Dn = board position n-1 in reading order\n"
     "(top -> bottom, left -> right), see the notes above.\n\n"
     "L0..L15 connect to R1..R16 in the MCU block.", MX0 + 14 * MDX + 17.78, 132.08, 1.27)
rect(MX0 + 14 * MDX + 15.24, 127.0, 406.4, 205.74)
by_pair = {g.charlie(n): n for n in range(g.N_LEDS)}
for a in range(g.N_LINES):
    Y = MY0 + a * MDY
    yb = Y - 3.81                                   # anode bus height
    xl = MX0 - 7.62                                 # bus label point
    taps = []
    cathodes = [c for c in range(g.N_LINES) if c != a]
    for k, c in enumerate(cathodes):
        n = by_pair[(a, c)]
        X = MX0 + k * MDX
        p = symbol("LED", f"D{n + 1}", X, Y, ref_at=(X, Y + 2.54), val_at=(X, Y + 5.08), hide_value=True)
        ax, ay = p["2"]                              # anode (right)
        wire((ax, ay), (ax, yb))
        taps.append((ax, yb))
        kx, ky = p["1"]                              # cathode (left)
        wire((kx, ky), (kx - 2.54, ky))
        label(f"L{c}", kx - 2.54, ky, right=False)
    wire((xl, yb), *taps)
    for t in taps[:-1]:
        junction(*t)
    label(f"L{a}", xl, yb, right=False)


# ============================ write ============================
def main():
    lib_syms = "\n".join(kl.lib_symbol_for_schematic(lib, name) for lib, name in sorted(used_libs))
    doc = f"""(kicad_sch
(version 20250610)
(generator "pixelpine_gen")
(generator_version "10.0")
(uuid "{ROOT_UUID}")
(paper "A3")
(title_block (title "PixelPine - 240 LED charlieplexed PCB Christmas tree") (date "2026-09-23") (rev "A")
 (company "KTE Project 1") (comment 1 "Generated by hardware/scripts/gen_sch.py - edit the script, not this file")
 (comment 2 "All LCSC part numbers in the LCSC field of each symbol"))
(lib_symbols
{lib_syms}
)
{chr(10).join(items)}
(sheet_instances (path "/" (page "1")))
(embedded_fonts no)
)
"""
    with open(OUT, "w", encoding="utf8", newline="\n") as fh:
        fh.write(doc)
    print("wrote", os.path.normpath(OUT), f"({len(items)} items)")


if __name__ == "__main__":
    main()
