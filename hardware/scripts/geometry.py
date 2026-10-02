"""PixelPine geometry: single source of truth for outline, LED positions and charlieplex map.

Coordinates are millimetres, origin at the top-centre of the star, y grows DOWN (KiCad convention).
Run directly to export hardware/led_map.json.
"""
import json
import math
import os

N_LINES = 16                      # charlieplex GPIO lines
N_LEDS = N_LINES * (N_LINES - 1)  # 240
HEIGHT = 70.0                     # total tree height incl. trunk

# MCU pin assigned to each charlieplex line L0..L15 (PY32F003F18P6, TSSOP-20 "pinout 1" pin numbers).
# PA13/PA14 double as SWD (programmed with connect-under-reset); PF4/BOOT0 has a reset-time pull-down, so it
# still boots from flash. Not lines: PA7 (ADC) = touch star, PF2 = NRST (kept for programming).
LINE_PINS = [
    ("PA0", 19), ("PA1", 20), ("PA2", 1), ("PA3", 2), ("PA4", 3), ("PA5", 4), ("PA6", 5), ("PA12", 8),
    ("PA13/SWDIO", 10), ("PA14/SWCLK", 11), ("PB5", 12), ("PB6", 13), ("PB7", 14), ("PF0", 16), ("PF1", 17),
    ("PF4/BOOT0", 15),
]
TOUCH_PIN, NRST_PIN = ("PA7", 6), ("PF2/NRST", 18)

# --- star: a "puffy" five-point star with rounded tips (touch pad + top of the outline) ---
STAR_C = (0.0, 4.6)
STAR_R, STAR_r = 4.6, 2.5           # tip / valley radius
STAR_ROUND = 1.1                    # tip sharpness exponent (lower = chubbier)

# --- foliage tiers: (hw_top, y_top, hw_tip, y_tip); edges bulge out, bottoms sag like a smile ---
TIERS = [
    (1.3, 8.9, 13.5, 23.8),
    (6.0, 18.0, 20.0, 39.6),
    (11.0, 33.0, 26.0, 55.2),
]
DROOP = 1.8            # tier tip hangs this much below the point where its bottom meets the next tier
SIDE_BULGE = 0.9       # chubby, slightly convex tier sides
BOTTOM_SAG = 1.3       # tier bottoms curve down between tip and join
TIP_R = 2.4            # corner rounding of the tier tips
TRUNK_HW = 11.0
FOLIAGE_BOTTOM_JOIN = 54.3
SLOT_W, SLOT_DEPTH = 1.7, 5.0      # cross-lap slot for the 1.6 mm foot

# --- foot (stand) fin, drawn in its own local frame, placed below the tree in the panel ---
FOOT_LEN, FOOT_H = 36.0, 10.0
FOOT_Y0 = HEIGHT + 3.0             # panel position (3 mm gap, mouse-bite tabs)

LED_EDGE_CLEARANCE = 1.85          # LED centre to board edge: 45deg pad corner (1.31) + 0.5 mm rule
LED_MIN_Y, LED_MAX_Y = 9.6, 53.2   # keep the star neck and trunk free


def star_radius(theta, r_out=None, r_in=None):
    """Polar radius of the puffy star; theta in radians, y-down frame, tip at -90 deg."""
    r_out = STAR_R if r_out is None else r_out
    r_in = STAR_r if r_in is None else r_in
    c = 0.5 + 0.5 * math.cos(5 * (theta + math.pi / 2))
    return r_in + (r_out - r_in) * c ** STAR_ROUND


def star_points(n=100, r_out=None, r_in=None):
    cx, cy = STAR_C
    pts = []
    for i in range(n):
        th = -math.pi / 2 + 2 * math.pi * i / n
        r = star_radius(th, r_out, r_in)
        pts.append((cx + r * math.cos(th), cy + r * math.sin(th)))
    return pts   # clockwise (y down) starting at the top tip


def _tier_x(t, y):
    hw0, y0, hw1, y1 = t
    return hw0 + (y - y0) * (hw1 - hw0) / (y1 - y0)


def _bulged(p, q, bulge, n=8, skew=False):
    """Points strictly between p and q along a gentle arc offset by `bulge` (to the right of p->q).
    skew=True puts the belly near p and fades to nothing at q (keeps concave joins clean)."""
    dx, dy = q[0] - p[0], q[1] - p[1]
    L = math.hypot(dx, dy)
    nx, ny = -dy / L, dx / L          # right-hand normal in a y-down frame points outward here
    out = []
    for k in range(1, n):
        u = k / n
        prof = math.sin(math.pi * u) * ((1 - u) ** 2 * 2.4 if skew else 1)
        out.append((p[0] + dx * u - nx * bulge * prof, p[1] + dy * u - ny * bulge * prof))
    return out


def _round_corners(pts, radius, keep):
    """Fillet every corner i (not in `keep`, radius[i] > 0) with a quadratic Bezier.

    The cut walks along the polygon, so it also works when the neighbours are densely sampled
    curve points: those inside the fillet are dropped."""
    n = len(pts)
    rad = [0.0 if (i in keep) else (radius[i] if isinstance(radius, (list, tuple)) else radius) for i in range(n)]

    def walk(i, step, r):
        """Point at arc distance r from pts[i] going in direction step, and indices passed over."""
        passed, acc, j = [], 0.0, i
        while True:
            k = (j + step) % n
            seg = math.dist(pts[j], pts[k])
            if acc + seg >= r or k == i:
                t = (r - acc) / seg if seg else 0
                t = min(t, 0.999)
                return (pts[j][0] + (pts[k][0] - pts[j][0]) * t, pts[j][1] + (pts[k][1] - pts[j][1]) * t), passed
            acc += seg
            passed.append(k)
            j = k

    drop, fillets = set(), {}
    for i in range(n):
        if rad[i] <= 0:
            continue
        s, pa = walk(i, -1, rad[i])
        e, pb = walk(i, +1, rad[i])
        drop |= set(pa) | set(pb)
        p = pts[i]
        fillets[i] = [((1 - t) ** 2 * s[0] + 2 * (1 - t) * t * p[0] + t * t * e[0],
                       (1 - t) ** 2 * s[1] + 2 * (1 - t) * t * p[1] + t * t * e[1])
                      for t in (k / 8 for k in range(9))]
    out = []
    for i in range(n):
        if i in fillets:
            out += fillets[i]
        elif i not in drop:
            out.append(pts[i])
    return out


_OUTLINE = None


def outline_polygon():
    """Closed tree outline (puffy star + 3 chubby tiers + trunk with bottom slot), clockwise."""
    global _OUTLINE
    if _OUTLINE is not None:
        return _OUTLINE
    neck = TIERS[0][0]
    cx, cy = STAR_C
    # where the star's lower-right flank meets the neck line x = neck (bisection on theta)
    lo, hi = math.radians(54), math.radians(90)
    for _ in range(60):
        mid = (lo + hi) / 2
        if star_radius(mid) * math.cos(mid) > neck:
            lo = mid
        else:
            hi = mid
    th_neck = lo
    right = []
    n = 60
    for i in range(n + 1):                                     # star, top tip -> neck
        th = -math.pi / 2 + (th_neck + math.pi / 2) * i / n
        r = star_radius(th)
        right.append((cx + r * math.cos(th), cy + r * math.sin(th)))
    keep = set(range(len(right)))                              # star is already smooth
    radius = [0.0] * len(right)

    def add(p, r=0.0, smooth=False):
        right.append(p)
        radius.append(r)
        if smooth:
            keep.add(len(right) - 1)

    add((neck, TIERS[0][1]), 0.9)
    for i, t in enumerate(TIERS):
        top = (t[0], t[1]) if i == 0 else right[-1]
        tip = (t[2], t[3])
        for q in _bulged(top, tip, SIDE_BULGE):
            add(q, smooth=True)
        add(tip, TIP_R)
        if i + 1 < len(TIERS):
            yj = t[3] - DROOP
            join = (_tier_x(TIERS[i + 1], yj), yj)
        else:
            join = (TRUNK_HW, FOLIAGE_BOTTOM_JOIN)
        for q in _bulged(tip, join, BOTTOM_SAG, skew=True):
            add(q, smooth=True)
        add(join, 0.6)
    add((TRUNK_HW, HEIGHT), 1.6)
    add((SLOT_W / 2, HEIGHT))
    add((SLOT_W / 2, HEIGHT - SLOT_DEPTH))
    keep |= {len(right) - 1, len(right) - 2}
    right = _round_corners(right, radius, keep)
    left = [(-x, y) for (x, y) in reversed(right[1:])]   # mirror, skip the star top (on the axis)
    _OUTLINE = right + left
    return _OUTLINE


def foot_outline():
    """Stand fin: a little snow drift, slot from the top centre, rounded feet. Panel coordinates."""
    L, H, y0 = FOOT_LEN, FOOT_H, FOOT_Y0
    steps = 16
    pts = []
    for i in range(steps + 1):
        x = -L / 2 + (L / 2 - SLOT_W / 2) * i / steps
        h = 3.5 + (H - 3.5) * (0.5 - 0.5 * math.cos(math.pi * i / steps))
        pts.append((x, y0 + H - h))
    pts.append((-SLOT_W / 2, y0 + SLOT_DEPTH))
    pts.append((SLOT_W / 2, y0 + SLOT_DEPTH))
    for i in range(steps, -1, -1):
        x = L / 2 - (L / 2 - SLOT_W / 2) * i / steps
        h = 3.5 + (H - 3.5) * (0.5 - 0.5 * math.cos(math.pi * i / steps))
        pts.append((x, y0 + H - h))
    pts.append((L / 2, y0 + H))
    pts.append((-L / 2, y0 + H))
    rad = [0.0] * len(pts)
    rad[0] = rad[-3] = 1.2          # soften the outer top corners
    rad[-2] = rad[-1] = 1.5         # rounded bottom corners
    keep = set(range(1, len(pts) - 3))
    return _round_corners(pts, rad, keep)


LED_ROT = 45.0    # footprint rotation, degrees


def point_in_poly(pt, poly):
    x, y = pt
    inside = False
    n = len(poly)
    for i in range(n):
        x1, y1 = poly[i]
        x2, y2 = poly[(i + 1) % n]
        if (y1 > y) != (y2 > y):
            if x < x1 + (y - y1) * (x2 - x1) / (y2 - y1):
                inside = not inside
    return inside


def dist_to_poly_edge(pt, poly):
    px, py = pt
    best = 1e9
    n = len(poly)
    for i in range(n):
        x1, y1 = poly[i]
        x2, y2 = poly[(i + 1) % n]
        dx, dy = x2 - x1, y2 - y1
        l2 = dx * dx + dy * dy
        t = 0 if l2 == 0 else max(0, min(1, ((px - x1) * dx + (py - y1) * dy) / l2))
        cx, cy = x1 + t * dx, y1 + t * dy
        best = min(best, math.hypot(px - cx, py - cy))
    return best


def _grid(pitch, yoff=0.0):
    """Square grid; LEDs sit at 45 deg so their pads point at the diagonal neighbours (sqrt2*pitch away)."""
    poly = outline_polygon()
    pts = []
    y = LED_MIN_Y + yoff
    while y <= LED_MAX_Y + 1e-9:
        k = -int(40 / pitch) - 1
        while True:
            x = k * pitch
            if x > 40:
                break
            p = (x, y)
            if point_in_poly(p, poly) and dist_to_poly_edge(p, poly) >= LED_EDGE_CLEARANCE:
                pts.append(p)
            k += 1
        y += pitch
    return pts


def _search():
    """Largest pitch (and vertical grid offset) giving 240..246 LEDs - little to trim, silhouette kept."""
    best = None
    p = 2.20
    while p >= 1.90 and best is None:
        for yoff in (0.0, 0.2, 0.4, 0.6, 0.8):
            n = len(_grid(p, yoff))
            if N_LEDS <= n <= N_LEDS + 6:
                best = (p, yoff)
                break
        p = round(p - 0.002, 3)
    return best


GRID_PITCH, GRID_YOFF = 2.12, 0.8   # cached result of _search(); re-run `python geometry.py --search` after outline edits


def led_positions():
    """Exactly N_LEDS positions on a square grid, reading order (top->bottom, left->right)."""
    pitch, yoff = GRID_PITCH, GRID_YOFF
    pts = _grid(pitch, yoff)
    poly = outline_polygon()
    # trim surplus symmetrically: drop the LEDs with the least edge clearance (mirror pairs together)
    while len(pts) > N_LEDS:
        excess = len(pts) - N_LEDS
        cands = sorted(pts, key=lambda q: (dist_to_poly_edge(q, poly), -q[1]))
        victim = cands[0]
        mirror = (-victim[0], victim[1])
        if excess >= 2 and abs(victim[0]) > 1e-6 and mirror in pts:
            pts.remove(victim)
            pts.remove(mirror)
        else:
            centre = [q for q in cands if abs(q[0]) < 1e-6]
            pts.remove(centre[0] if centre else victim)
    pts.sort(key=lambda p: (round(p[1], 3), p[0]))
    return pitch, pts


ASSIGN_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "led_assignment.json")
_ASSIGN = None


def _assignment():
    """[(anode line, cathode line, flipped)] for every LED position (reading order), from led_assignment.json.

    optimize_leds.py writes that file. It decides which of the 240 anode/cathode pairs sits at which grid position
    (and, optionally, which way round the LED is soldered) so that the 16 line nets form a few big copper islands
    joined by tiny F.Cu bridges and need very little inner-layer wiring: the board routes on 2 layers."""
    global _ASSIGN
    if _ASSIGN is None:
        with open(ASSIGN_FILE, encoding="utf8") as f:
            _ASSIGN = [(e[0], e[1], e[2] if len(e) > 2 else 0) for e in json.load(f)["leds"]]
    return _ASSIGN


def charlie(n):
    """LED n (reading order) -> (anode line, cathode line)."""
    a, c, _ = _assignment()[n]
    return a, c


def led_flip(n):
    """True when LED n is soldered turned round by 180 degrees (anode on the lower-left pad)."""
    return bool(_assignment()[n][2])


def _segments_cross(p1, p2, p3, p4):
    d = lambda a, b, c: (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
    return d(p1, p2, p3) * d(p1, p2, p4) < 0 and d(p3, p4, p1) * d(p3, p4, p2) < 0


def check_simple(poly):
    n = len(poly)
    for i in range(n):
        for j in range(i + 2, n):
            if (j + 1) % n != i and _segments_cross(poly[i], poly[(i + 1) % n], poly[j], poly[(j + 1) % n]):
                raise AssertionError(f"outline self-intersects near {poly[i]} / {poly[j]}")


def _self_check(pitch, pts):
    check_simple(outline_polygon())
    check_simple(foot_outline())
    assert len(pts) == N_LEDS, len(pts)
    pairs = {charlie(n) for n in range(N_LEDS)}
    assert len(pairs) == N_LEDS and all(a != c for a, c in pairs)
    poly = outline_polygon()
    for p in pts:
        assert point_in_poly(p, poly) and dist_to_poly_edge(p, poly) >= LED_EDGE_CLEARANCE - 1e-6
    dmin = min(math.dist(p, q) for i, p in enumerate(pts) for q in pts[i + 1:])
    return dmin


def export(path):
    pitch, pts = led_positions()
    dmin = _self_check(pitch, pts)
    data = {
        "units": "mm", "origin": "star top centre, y down", "height": HEIGHT,
        "pitch": round(pitch, 4), "min_spacing": round(dmin, 4),
        "lines": [p for p, _ in LINE_PINS],
        "leds": [{"id": n, "x": round(x, 4), "y": round(y, 4), "a": charlie(n)[0], "c": charlie(n)[1],
                  "flip": int(led_flip(n))}
                 for n, (x, y) in enumerate(pts)],
        "outline": [[round(x, 4), round(y, 4)] for x, y in outline_polygon()],
        "star": [[round(x, 4), round(y, 4)] for x, y in star_points()],
    }
    for led in data["leds"]:           # integer grid coordinates (handy for cellular animations)
        led["gx"] = round(led["x"] / pitch)
        led["gy"] = round((led["y"] - LED_MIN_Y - GRID_YOFF) / pitch)
    data["star_c"] = list(STAR_C)
    data["led_rot"] = LED_ROT
    with open(path, "w") as f:
        json.dump(data, f, indent=1)
    # same data as a script for the simulator (works from file:// without fetch)
    sim = os.path.join(os.path.dirname(os.path.abspath(path)), "..", "simulator", "led_map.js")
    os.makedirs(os.path.dirname(sim), exist_ok=True)
    with open(sim, "w") as f:
        f.write("// Generated by hardware/scripts/geometry.py - do not edit\n")
        f.write("window.LED_MAP = " + json.dumps(data, separators=(",", ":")) + ";\n")
    return pitch, dmin


if __name__ == "__main__":
    import sys
    if "--search" in sys.argv:
        print("best (pitch, yoff):", _search())
        sys.exit()
    out = os.path.join(os.path.dirname(__file__), "..", "led_map.json")
    pitch, dmin = export(out)
    print(f"LEDs={N_LEDS} pitch={pitch:.3f} mm  min spacing={dmin:.3f} mm -> {os.path.normpath(out)}")
