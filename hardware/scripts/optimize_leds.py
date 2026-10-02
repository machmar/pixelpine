"""Choose which charlieplex pair sits at which LED position -> hardware/led_assignment.json.

The 240 LED positions are fixed (geometry.py); only the assignment of anode/cathode pairs to them is optimised.
Random or "interleaved" assignments leave every one of the 480 pads as its own island, which no autorouter can
connect. The 45 degree LEDs form a "brick" lattice whose neighbouring pads are only ~0.55 mm apart, so two
neighbouring pads on the SAME net are joined by a tiny F.Cu bridge. optimize_leds.c (simulated annealing) arranges
the pairs so each of the 16 line nets becomes a handful of such copper islands (~140 instead of ~480); the islands
of a net are then linked with a little inner wiring, and the board routes on 2 layers.

    python optimize_leds.py [--iters N] [--seeds K] [--flip] [--init FILE] [--out FILE]

  --iters  annealing steps per run (default 20 000 000; the default 4 runs take 10-20 min together)
  --seeds  independent runs, executed in parallel; the cheapest one wins (default 4)
  --flip   also allow soldering an LED turned round by 180 degrees (about 15 % fewer islands, unused so far:
           the arrangement routes without it and every LED keeps the same orientation)
  --init   start from an existing led_assignment.json instead of a random arrangement (refine it)

Needs a C compiler (gcc on PATH or MSYS2's at C:/msys64/mingw64/bin). The result is committed, so build_all.py
never runs this: only re-run it after changing the outline, the LED grid or the cost weights.
Afterwards run build_all.py (the schematic, PCB, simulator map and firmware tables all follow from the file).
"""
import argparse
import json
import math
import os
import shutil
import subprocess
import sys
import tempfile

import geometry as g

HERE = os.path.dirname(os.path.abspath(__file__))
HW = os.path.normpath(os.path.join(HERE, ".."))
OUT = os.path.join(HW, "led_assignment.json")
SRC = os.path.join(HERE, "optimize_leds.c")
PAD = 0.557            # pad centre offset along x and y of a 45 degree LED (0.7875 / sqrt 2)
BRIDGE = 1.55          # pads closer than this (centre to centre, different LEDs) are joined by an F.Cu bridge


def find_gcc():
    exe = shutil.which("gcc") or shutil.which("cc")
    for cand in ("C:/msys64/mingw64/bin/gcc.exe", "C:/msys64/ucrt64/bin/gcc.exe"):
        exe = exe or (cand if os.path.exists(cand) else None)
    if not exe:
        sys.exit("no C compiler found (install MSYS2 gcc or put gcc on PATH)")
    return exe


def islands(assign, pts):
    """Number of copper islands once same-net neighbouring pads are bridged (and the net's pad list)."""
    pads = []
    for n, (a, c, f) in enumerate(assign):
        x, y = pts[n]
        pads.append((x + PAD, y - PAD, c if f else a, n))     # up-right pad
        pads.append((x - PAD, y + PAD, a if f else c, n))     # down-left pad
    parent = list(range(len(pads)))

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i
    for i in range(len(pads)):
        for j in range(i + 1, len(pads)):
            if (pads[i][2] == pads[j][2] and pads[i][3] != pads[j][3]
                    and math.hypot(pads[i][0] - pads[j][0], pads[i][1] - pads[j][1]) < BRIDGE):
                parent[find(i)] = find(j)
    return len({find(i) for i in range(len(pads))})


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--iters", type=int, default=20_000_000)
    ap.add_argument("--seeds", type=int, default=4)
    ap.add_argument("--flip", action="store_true")
    ap.add_argument("--init")
    ap.add_argument("--out", default=OUT)
    args = ap.parse_args()

    _, pts = g.led_positions()
    work = tempfile.mkdtemp(prefix="pixelpine_opt_")
    exe = os.path.join(work, "optimize_leds.exe")
    subprocess.run([find_gcc(), "-O3", "-march=native", "-o", exe, SRC, "-lm"], check=True)
    pos = os.path.join(work, "positions.txt")
    with open(pos, "w") as f:
        f.write(f"{len(pts)}\n" + "".join(f"{x:.4f} {y:.4f}\n" for x, y in pts))
    init = []
    if args.init:
        with open(args.init, encoding="utf8") as f:
            rows = json.load(f)["leds"]
        init_file = os.path.join(work, "init.txt")
        with open(init_file, "w") as f:
            f.write("".join(f"{r[0]} {r[1]} {r[2] if len(r) > 2 else 0}\n" for r in rows))
        init = ["-init", init_file]

    procs = []
    for seed in range(1, args.seeds + 1):
        out = os.path.join(work, f"out{seed}.txt")
        cmd = [exe, pos, str(args.iters), str(seed), out, "-T0", "0.5" if init else "2.0"] + (["-flip"] if args.flip else []) + init
        procs.append((seed, out, subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)))
    best = None
    for seed, out, p in procs:
        stdout = p.communicate()[0]
        line = [l for l in stdout.splitlines() if l.startswith("final")][-1]
        cost = float(line.split("cost=")[1].split()[0])
        print(f"seed {seed}: {line}")
        if best is None or cost < best[0]:
            best = (cost, seed, out)
    cost, seed, out = best
    with open(out) as f:
        assign = [tuple(int(v) for v in l.split()) for l in f if l.strip()]
    pairs = {(a, c) for a, c, _ in assign}
    assert len(assign) == g.N_LEDS and len(pairs) == g.N_LEDS and all(a != c for a, c in pairs), "not a permutation"
    isl = islands(assign, pts)
    with open(args.out, "w", encoding="utf8", newline="\n") as f:
        json.dump({"generator": "scripts/optimize_leds.py", "seed": seed, "iterations": args.iters, "cost": cost,
                   "copper_islands": isl, "leds": [list(e) for e in assign]}, f)
    shutil.rmtree(work, ignore_errors=True)
    print(f"wrote {args.out}: {isl} copper islands, cost {cost:.1f}")


if __name__ == "__main__":
    main()
