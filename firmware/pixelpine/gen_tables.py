"""Generate src/tables.c + src/geom.h: every per-LED constant and maths table the firmware needs.

The simulator (simulator/index.html) computes these on the fly in floating point. The MCU has no FPU, so all the
per-LED trigonometry runs here once, on the PC, in the same double-precision formulas as the simulator, and the
firmware reads the results from flash. Input: hardware/led_map.json (written by hardware/scripts/geometry.py).

Number formats (see src/fx.h): Q12 = value * 4096, Q16 = value * 65536, "turns" = angle / 2pi * 2^32 (uint32,
wraps exactly like an angle).
"""
import json
import math
import os
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
M = json.load(open(os.path.join(HERE, "..", "..", "hardware", "led_map.json"), encoding="utf8"))
L = M["leds"]
N = len(L)
P = M["pitch"]
SC = M["star_c"]
f32 = lambda v: struct.unpack("<f", struct.pack("<f", v))[0]          # the simulator keeps these in Float32Arrays
LX = [f32(l["x"]) for l in L]
LY = [f32(l["y"]) for l in L]
GX = [l["gx"] for l in L]
GY = [l["gy"] for l in L]
Y0, Y1 = min(LY), max(LY)
GXMIN, GXMAX, GYMIN, GYMAX = min(GX), max(GX), min(GY), max(GY)
GW, GH = GXMAX - GXMIN + 1, GYMAX - GYMIN + 1

# --- the simulator's derived per-LED values (index.html, "geometry" block), same formulas ---
row_half = {}
for i in range(N):
    row_half[GY[i]] = max(row_half.get(GY[i], 0), abs(LX[i]) + P * 0.5)
U = [f32(LX[i] / 22) for i in range(N)]
V = [f32((LY[i] - Y0) / (Y1 - Y0)) for i in range(N)]
DS = [f32(math.hypot(LX[i] - SC[0], LY[i] - SC[1])) for i in range(N)]
REL = [f32(LX[i] / row_half[GY[i]]) for i in range(N)]
col_bottom = {}
for i in range(N):
    col_bottom[GX[i]] = max(col_bottom.get(GX[i], -1), GY[i])
TH = [math.asin(max(-0.999, min(0.999, r))) for r in REL]
ANG = [f32(math.atan2(LY[i] - SC[1], LX[i] - SC[0])) for i in range(N)]
star_r = lambda th: 2.5 + 2.1 * math.pow(0.5 + 0.5 * math.cos(5 * (th + math.pi / 2)), 1.1)
VC, VH = (Y0 + Y1) / 2, (Y1 - Y0) / 2 + 1


def imul(a, b):
    return (a * b) & 0xFFFFFFFF


def hash_u(x):
    x &= 0xFFFFFFFF
    x = imul(x ^ (x >> 16), 0x7FEB352D)
    x = imul(x ^ (x >> 15), 0x846CA68B)
    x ^= x >> 16
    return x / 4294967296


hash1 = lambda i: hash_u(imul(i, 0x9E3779B1))
hash2 = lambda x, y: hash_u(imul(x, 374761393) + imul(y, 668265263))

q12 = lambda v: int(round(v * 4096))
q16 = lambda v: int(round(v * 65536))
turns = lambda rad: int(round(rad / (2 * math.pi) * 4294967296)) & 0xFFFFFFFF
fturn = lambda f: int(round((f - math.floor(f)) * 4294967296)) & 0xFFFFFFFF      # fraction of a turn


def to_i32(u):
    return u - (1 << 32) if u >= 1 << 31 else u


def arr(ctype, name, vals, per=16):
    vals = list(vals)
    rows = [", ".join(str(v) for v in vals[k:k + per]) for k in range(0, len(vals), per)]
    return f"const {ctype} {name}[{len(vals)}] = {{\n  " + ",\n  ".join(rows) + "\n};\n"


T = {}
T["int16_t led_v"] = [q12(v) for v in V]                         # 0 top .. 1 bottom
T["uint16_t led_ds"] = [int(round(v * 1024)) for v in DS]          # distance from the star centre, mm Q10
T["int8_t led_gx"] = GX
T["int8_t led_gy"] = GY
grid = [255] * (GW * GH)
for i in range(N):
    grid[(GY[i] - GYMIN) * GW + GX[i] - GXMIN] = i
T["uint8_t grid"] = grid
T["int8_t col_bottom"] = [col_bottom.get(g, -1) for g in range(GXMIN, GXMAX + 1)]
T["int32_t row_half"] = [q12(row_half.get(g, 4)) for g in range(GYMIN, GYMAX + 1)]
T["uint16_t led_th"] = [turns(t) >> 16 for t in TH]                  # garland / ornaments cone angle
T["int16_t led_starr"] = [q12(star_r(a)) for a in ANG]           # star-iris radius at this LED's angle
# light-show constants
T["uint32_t ra_phi"] = [turns(math.atan2(LX[i] - SC[0], LY[i] - SC[1])) for i in range(N)]   # radar, 0 = down
HD = [math.hypot(LX[i], LY[i] - 30) for i in range(N)]                              # heartbeat
T["uint16_t hb_d55"] = [int(round(d / 55 * 32768)) for d in HD]                    # Q15 seconds
T["int16_t hb_k"] = [q12(1 - 0.45 * min(1, d / 35)) for d in HD]
T["uint16_t ti_ph"] = [(turns(LX[i] * 0.55 + GY[i] * 0.9 + hash2(GY[i], 3) * 6.28) >> 16) for i in range(N)]   # tinsel
T["uint16_t ti_c"] = [turns(hash1(i + 999) * 40) >> 16 for i in range(N)]

# snowfall melt: the simulator keeps the snow layer in a Float32Array and melts it by dt*0.22*(0.4+(1-V)) per frame;
# frames until each LED's snow reaches exactly 0, with that float32 rounding
def melt_frames(v):
    s, c, k = 1.0, 0.02 * 0.22 * (0.4 + (1 - v)), 0
    while s > 0:
        s = f32(max(0.0, s - c)); k += 1
    return k
T["uint16_t snow_k"] = [melt_frames(V[i]) for i in range(N)]
T["float row_y"] = [f32(LY[0] - GY[0] * P + g * P) for g in range(GYMIN, GYMAX + 1)]   # LY of each grid row

# --- shared maths tables ---
T["uint16_t sin_tab"] = [int(round(math.sin(k / 1024 * 2 * math.pi) * 65535)) for k in range(257)]   # quarter wave, Q16
T["uint16_t exp2_tab"] = [int(round(2 ** (-k / 64) * 65535)) for k in range(65)]                  # 2^-f, Q16
T["uint16_t pow16_tab"] = [q12(math.pow(k / 256, 1.6)) for k in range(257)]
T["uint16_t pow12_tab"] = [q12(math.pow(k / 256, 1.2)) for k in range(257)]
T["uint16_t pow14_tab"] = [q12(math.pow(k / 256, 1.4)) for k in range(257)]
# gamma 2.2 + 8-bit quantisation exactly as the simulator's "raw scan" model: Q = round(255 * x^2.2)
# gamma_thr[q] = smallest Q12 value whose level is >= q + 1, so level(v) = number of thresholds <= v
gam = [int(math.floor(255 * math.pow(v / 4096, 2.2) + 0.5)) for v in range(4097)]
T["uint16_t gamma_thr"] = [next(v for v in range(4097) if gam[v] >= q) for q in range(1, 256)] + [4097]

# --- charlieplex scan: the LEDs of each anode line and their cathode lines (hardware/led_assignment.json) ---
by_anode = [[(i, L[i]["c"]) for i in range(N) if L[i]["a"] == a] for a in range(16)]
assert all(len(p) == 15 for p in by_anode), "every anode line must drive exactly 15 LEDs"
T["uint8_t phase_led"] = [i for p in by_anode for i, _ in p]
T["uint8_t phase_cath"] = [c for p in by_anode for _, c in p]

out = ["/* Generated by gen_tables.py from hardware/led_map.json - do not edit */", '#include "pine.h"', ""]
hdr = ["/* Generated by gen_tables.py from hardware/led_map.json - do not edit */", "#pragma once",
       "#include <stdint.h>", ""]
for key, vals in T.items():
    ctype, name = key.split()
    if ctype in ("int32_t",):
        vals = [to_i32(v & 0xFFFFFFFF) if v < 0 or v >= 1 << 31 else v for v in vals]
    if ctype == "float":
        vals = [repr(v) + ("f" if "." in repr(v) or "e" in repr(v) else ".0f") for v in vals]
    out.append(arr(ctype, name, vals))
    hdr.append(f"extern const {ctype} {name}[{len(vals)}];")
hdr += ["",
        f"#define NLED {N}",
        f"#define GXMIN ({GXMIN})", f"#define GXMAX {GXMAX}", f"#define GYMIN {GYMIN}", f"#define GYMAX {GYMAX}",
        f"#define GW {GW}", f"#define GH {GH}",
        "/* the simulator's scalar geometry, as doubles (for per-frame maths) and Q12 (per-LED maths) */",
        f"#define PITCH {P!r}", f"#define LED_Y0 {Y0!r}", f"#define LED_Y1 {Y1!r}",
        f"#define STAR_X {SC[0]!r}", f"#define STAR_Y {SC[1]!r}",
        f"#define ROW_HALF_MAX {row_half[GYMAX]!r}   /* rowHalf[gyMax] */",
        f"#define PITCH_Q {q12(P)}", f"#define LED_Y0_Q {q12(Y0)}", f"#define LED_Y1_Q {q12(Y1)}",
        f"#define INV_PITCH_Q16 {q16(1 / P)}",
        f"#define BLINK_C_Q {q12((Y0 + Y1) / 2)}", f"#define BLINK_INVH_Q16 {q16(1 / ((Y1 - Y0) / 2 + 1))}",
        f"#define LED_YBASE_Q {q12(LY[0] - GY[0] * P)}   /* LY = YBASE + gy * P (the LEDs sit on an exact grid) */",
        f"#define PITCH_Q4 {int(round(P * 4096 * 4))}", ""]
assert all(abs(LX[i] - GX[i] * P) < 1e-5 and abs(LY[i] - (LY[0] - GY[0] * P) - GY[i] * P) < 1e-5 for i in range(N))
with open(os.path.join(HERE, "src", "tables.c"), "w", newline="\n") as f:
    f.write("\n".join(out))
with open(os.path.join(HERE, "src", "geom.h"), "w", newline="\n") as f:
    f.write("\n".join(hdr))
print(f"tables.c: {N} LEDs, grid {GW}x{GH}, {len(T)} tables")
