"""Generate hardware/pixelpine.kicad_pcb: outline, stand, placed footprints with nets, 2 copper layers (route.py autoroutes it).

Run with KiCad's bundled Python (it provides `pcbnew`):
    "C:/Program Files/KiCad/10.0/bin/python.exe" gen_pcb.py
Footprints + nets come from the schematic netlist (exported via kicad-cli), positions from geometry.py.
"""
import os
import re
import subprocess

import pcbnew

import geometry as g

HERE = os.path.dirname(os.path.abspath(__file__))
HW = os.path.normpath(os.path.join(HERE, ".."))
KICAD = r"C:/Program Files/KiCad/10.0"
KICAD_CLI = os.path.join(KICAD, "bin", "kicad-cli.exe")
FP_ROOT = os.path.join(KICAD, "share", "kicad", "footprints")
PROJ_LIB = os.path.join(HW, "PixelPine.pretty")   # written by gen_footprints.py
SCH = os.path.join(HW, "pixelpine.kicad_sch")
PCB = os.path.join(HW, "pixelpine.kicad_pcb")

OX, OY = 100.0, 40.0        # sheet offset of the geometry origin (star top-centre)

# ------------------------------------------------------------ placement (geometry mm, y down)
TRUNK = {
    # ref: (x, y, rotation_deg, side)
    "U1": (-6.2, 62.4, 0, "F"),
    "C2": (-6.2, 67.0, 0, "F"),
    "J1": (11.0 + 0.1 - 4.626, 63.8, 90, "F"),   # mouth (4.626 mm from the LCSC origin) 0.1 mm past the trunk edge
    "C1": (2.4, 66.6, 90, "F"),
    "R17": (2.4, 60.9, 90, "F"),
    "R18": (2.4, 62.9, 90, "F"),
    "R19": (-9.5, 58.2, 0, "F"),            # 1k ESD series resistor for the touch star
    # SWD pogo pads on the back, two rows: (SWDIO, GND, 5V) and (SWCLK, NRST); SWDIO sits next to the MCU pin 10
    # and the thick power tracks reach 5V from the other side (the other way round, the router boxed SWDIO in)
    "TP1": (-3.6, 68.3, 0, "B"),
    "TP2": (-6.1, 68.3, 0, "B"),
    "TP3": (-8.6, 68.3, 0, "B"),
    "TP5": (-8.6, 65.0, 0, "B"),
    "TP6": (-6.1, 65.0, 0, "B"),
    "TP4": (g.STAR_C[0], g.STAR_C[1], 0, "F"),
}
for i in range(g.N_LINES):                 # line resistors: one row across the top of the trunk
    TRUNK[f"R{i + 1}"] = (-9.0 + i * 1.2, 56.3, 90, "F")
FOOT_KEEPOUT = (-1.5, 59.5, 1.5, g.HEIGHT)  # front strip where the stand fin crosses the trunk


def mm(x, y):
    return pcbnew.VECTOR2I_MM(OX + x, OY + y)


# ------------------------------------------------------------ netlist
def read_netlist():
    net_path = os.path.join(HW, "renders", "_pixelpine.net")
    subprocess.run([KICAD_CLI, "sch", "export", "netlist", "--format", "kicadsexpr", "-o", net_path, SCH],
                   check=True, capture_output=True)
    s = open(net_path, encoding="utf8").read()
    os.remove(net_path)
    comps = {}
    comp_sec = s[s.index("(components"):s.index("(libparts")]
    for blk in comp_sec.split("(comp\n")[1:]:
        ref = re.search(r'\(ref "([^"]+)"\)', blk).group(1)
        comps[ref] = dict(
            value=re.search(r'\(value "([^"]*)"\)', blk).group(1),
            footprint=re.search(r'\(footprint "([^"]*)"\)', blk).group(1),
            uuid=re.findall(r'\(tstamps "([^"]+)"\)', blk)[-1],
            lcsc=(re.findall(r'\(field\s*\(name "LCSC"\)\s*"([^"]*)"\)', blk) or [""])[0],
        )
    pad_net = {}
    for blk in s[s.index("(nets"):].split("(net\n")[1:]:
        name = re.search(r'\(name "([^"]*)"\)', blk).group(1)
        for ref, pin in re.findall(r'\(ref "([^"]+)"\)\s*\(pin "([^"]+)"\)', blk):
            pad_net[(ref, pin)] = name
    return comps, pad_net


def load_fp(lib_fp):
    lib, name = lib_fp.split(":")
    path = PROJ_LIB if lib == "PixelPine" else os.path.join(FP_ROOT, lib + ".pretty")
    fp = pcbnew.FootprintLoad(path, name)
    if fp is None:
        raise RuntimeError(f"footprint not found: {lib_fp}")
    fp.SetFPID(pcbnew.LIB_ID(lib, name))
    return fp


# ------------------------------------------------------------ board drawing helpers
def poly_edges(board, pts, layer=pcbnew.Edge_Cuts, width=0.1):
    n = len(pts)
    for i in range(n):
        seg = pcbnew.PCB_SHAPE(board)
        seg.SetShape(pcbnew.SHAPE_T_SEGMENT)
        seg.SetStart(mm(*pts[i]))
        seg.SetEnd(mm(*pts[(i + 1) % n]))
        seg.SetLayer(layer)
        seg.SetWidth(pcbnew.FromMM(width))
        board.Add(seg)


def text(board, s, x, y, size=1.0, layer=pcbnew.F_SilkS, bold=False, thickness=None):
    t = pcbnew.PCB_TEXT(board)
    t.SetText(s)
    t.SetPosition(mm(x, y))
    t.SetLayer(layer)
    t.SetTextSize(pcbnew.VECTOR2I_MM(size, size))
    t.SetTextThickness(pcbnew.FromMM(thickness or size * (0.2 if bold else 0.15)))
    if layer in (pcbnew.B_SilkS, pcbnew.B_Fab):
        t.SetMirrored(True)
    board.Add(t)
    return t


def place_logo(board, lib_fp, x, y, ref):
    fp = load_fp(lib_fp)
    fp.SetReference(ref)               # unique refs: the Specctra (autorouter) export rejects duplicates
    board.Add(fp)                      # Flip() needs the parent board
    fp.SetPosition(mm(x, y))
    fp.Flip(fp.GetPosition(), pcbnew.FLIP_DIRECTION_LEFT_RIGHT)
    fp.SetBoardOnly(True)
    fp.SetExcludedFromBOM(True)
    fp.SetExcludedFromPosFiles(True)
    fp.GetField(pcbnew.FIELD_T_REFERENCE).SetVisible(False)
    fp.GetField(pcbnew.FIELD_T_VALUE).SetVisible(False)


def snowflakes(board, led_pts):
    """Tiny silkscreen snowflakes in the LED-free tier tips (free decoration: silk costs nothing)."""
    import math
    outline = g.outline_polygon()
    placed = 0
    for i, t in enumerate(g.TIERS):
        for side in (-1, 1):
            tip = (side * t[2], t[3])
            # step inward along the tier edge until there is room for a 1 mm flake
            for k in range(8, 40):
                c = (tip[0] - side * k * 0.25, tip[1] - k * 0.22)
                if not g.point_in_poly(c, outline) or g.dist_to_poly_edge(c, outline) < 1.6:
                    continue
                if min(math.dist(c, q) for q in led_pts) < 2.5:
                    continue
                for a in range(3):                       # three crossed spokes + little tips
                    ang = math.radians(90 + 60 * a)
                    dx, dy = math.cos(ang) * 1.0, math.sin(ang) * 1.0
                    line(board, (c[0] - dx, c[1] - dy), (c[0] + dx, c[1] + dy))
                    for sgn in (-1, 1):
                        ex, ey = c[0] + sgn * dx * 0.65, c[1] + sgn * dy * 0.65
                        for off in (-35, 35):
                            b = ang + math.radians(off) + (math.pi if sgn < 0 else 0)
                            line(board, (ex, ey), (ex - math.cos(b) * 0.35, ey - math.sin(b) * 0.35))
                placed += 1
                break
    return placed


def line(board, p, q, layer=pcbnew.F_SilkS, width=0.15):
    seg = pcbnew.PCB_SHAPE(board)
    seg.SetShape(pcbnew.SHAPE_T_SEGMENT)
    seg.SetStart(mm(*p))
    seg.SetEnd(mm(*q))
    seg.SetLayer(layer)
    seg.SetWidth(pcbnew.FromMM(width))
    board.Add(seg)


def set_design_rules(board, margin=0.0):
    """JLC standard 1-2 layer process, no extra-cost options (jlcpcb.com/capabilities/pcb-capabilities): tracks and
    spaces down to 0.10 mm, vias 0.15 mm hole / 0.25 mm pad, via hole to copper 0.2 mm.
    Routing rules are a bit looser than that on purpose: tracks 0.127 mm (the router may neck down to the 0.10 mm
    minimum between LED pads), clearance 0.15 mm because the autorouter knows nothing about the 0.2 mm
    via-hole-to-copper rule and 0.15 mm from the 0.4 mm via pad keeps the drill 0.2 mm away. Vias 0.3 mm drill /
    0.4 mm pad. Power nets get 0.2 mm tracks (the whole tree draws about 20 mA). The autorouter (route.py) applies
    these to the board too; it asks for `margin` mm more clearance, which pays for widening its 0.095 mm necks."""
    mmv = pcbnew.FromMM
    ds = board.GetDesignSettings()
    ds.m_TrackMinWidth = mmv(0.1)
    ds.m_MinClearance = mmv(0.1)
    ds.m_HoleClearance = mmv(0.2)
    ds.m_ViasMinSize = mmv(0.4)
    ds.m_ViasMinAnnularWidth = mmv(0.05)
    ds.m_MinThroughDrill = mmv(0.3)
    ds.m_CopperEdgeClearance = mmv(0.3)
    ns = ds.m_NetSettings
    default = ns.GetDefaultNetclass()
    default.SetTrackWidth(mmv(0.127))
    default.SetClearance(mmv(0.15 + margin))
    default.SetViaDiameter(mmv(0.4))
    default.SetViaDrill(mmv(0.3))
    power = pcbnew.NETCLASS("Power")
    power.SetTrackWidth(mmv(0.2))
    power.SetClearance(mmv(0.15 + margin))
    power.SetViaDiameter(mmv(0.5))
    power.SetViaDrill(mmv(0.3))
    ns.SetNetclass("Power", power)
    for pattern in ("GND", "+5V"):
        ns.SetNetclassPatternAssignment(pattern, "Power")
    ns.ClearAllCaches()


# ------------------------------------------------------------ main
def main():
    comps, pad_net = read_netlist()
    _, led_pts = g.led_positions()

    board = pcbnew.NewBoard(PCB)
    board.SetCopperLayerCount(2)            # routable on 2 layers thanks to the LED arrangement (led_assignment.json)
    set_design_rules(board)

    nets = {}
    for name in sorted(set(pad_net.values())):
        ni = pcbnew.NETINFO_ITEM(board, name)
        board.Add(ni)
        nets[name] = ni

    for ref, c in sorted(comps.items()):
        fp = load_fp(c["footprint"])
        fp.SetReference(ref)
        fp.SetValue(c["value"])
        fp.SetPath(pcbnew.KIID_PATH("/" + c["uuid"]))
        if c["lcsc"]:
            fp.SetField("LCSC", c["lcsc"])
            fp.GetField("LCSC").SetVisible(False)
        if ref.startswith("D"):
            n = int(ref[1:]) - 1
            x, y = led_pts[n]
            rot, side = g.LED_ROT + (180.0 if g.led_flip(n) else 0.0), "F"
        else:
            x, y, rot, side = TRUNK[ref]
        board.Add(fp)
        fp.SetPosition(mm(x, y))
        fp.SetOrientationDegrees(rot)
        if side == "B":
            fp.Flip(fp.GetPosition(), pcbnew.FLIP_DIRECTION_LEFT_RIGHT)
        for pad in fp.Pads():
            net = pad_net.get((ref, pad.GetNumber()))
            if net:
                pad.SetNet(nets[net])
        fp.GetField(pcbnew.FIELD_T_REFERENCE).SetVisible(False)
        fp.GetField(pcbnew.FIELD_T_VALUE).SetVisible(False)

    # ---- outlines: tree and the stand fin (separate piece, panelised with mouse bites at the fab)
    poly_edges(board, g.outline_polygon())
    poly_edges(board, g.foot_outline())

    # ---- documentation layers
    kx0, ky0, kx1, ky1 = FOOT_KEEPOUT
    poly_edges(board, [(kx0, ky0), (kx1, ky0), (kx1, ky1), (kx0, ky1)], layer=pcbnew.Cmts_User, width=0.05)
    text(board, "stand fin keep-out (front)", 0, 58.8, 0.5, pcbnew.Cmts_User)

    # ---- front silk: foot + trunk
    text(board, "PixelPine", 0, g.FOOT_Y0 + 8.2, 1.2, pcbnew.F_SilkS, bold=True)
    snowflakes(board, led_pts)

    # ---- back silk: product marking required by GPSR / CE / WEEE (see docs/eu-compliance)
    B = pcbnew.B_SilkS
    text(board, "PixelPine", 0, 22.0, 2.4, B, bold=True)
    text(board, "rev A", 0, 25.4, 0.9, B)
    text(board, "5 V  0.1 A  USB-C", 0, 30.0, 1.0, B)
    text(board, "Model PP-240-WW", 0, 32.0, 0.9, B)
    text(board, "Batch: ____  Date: __/__", 0, 34.0, 0.9, B)
    text(board, "MachStuff s.r.o.", 0, 36.6, 0.9, B)
    text(board, "Jehličková 240", 0, 38.4, 0.9, B)           # made-up address (school project)
    text(board, "240 24 Stromečkov, CZ", 0, 40.2, 0.9, B)          # two lines: the tier notches narrow the board here
    text(board, "marekmach24@gmail.com", 0, 42.0, 0.9, B)
    text(board, "Indoor use only. Not a toy.", 0, 44.0, 0.9, B)
    text(board, "Hi, I'm Pine! Boop my star.", 0, 45.8, 0.8, B)
    place_logo(board, "Symbol:CE-Logo_8.5x6mm_SilkScreen", -7.0, 50.4, "LOGO1")
    place_logo(board, "Symbol:WEEE-Logo_4.2x6mm_SilkScreen", 6.0, 50.4, "LOGO2")
    text(board, "5V GND DIO", -6.1, 66.65, 0.8, B)      # mirrored on the back: reads right-to-left in x
    text(board, "RST CLK", -7.35, 63.35, 0.8, B)

    board.SynchronizeNetsAndNetClasses(True)
    pcbnew.SaveBoard(PCB, board)
    print("wrote", PCB, f"footprints={len(board.GetFootprints())} nets={len(nets)}")


if __name__ == "__main__":
    main()
