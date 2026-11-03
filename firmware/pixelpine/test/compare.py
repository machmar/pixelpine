"""Compare the firmware (PC build) with the simulator's own JavaScript, frame by frame.

  python test/compare.py            every light show (30 s each) + whole-Pine scenarios
  python test/compare.py shows      light shows only
  python test/compare.py runs       scenarios only

Both sides get the same seeds, the same random-number stream and the same touch input (test/simref.js runs
simulator/index.html; build/pine_host.exe runs src/). What is compared is what the LEDs show: each LED's 8-bit
BCM level after gamma 2.2, Q = round(255 * v^2.2), as in the simulator's "Raw scan" hardware model.
"""
import os
import struct
import subprocess
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
BUILD = os.path.join(ROOT, "build")
NODE = os.environ.get("NODE", r"C:\Program Files\nodejs\node.exe")
HOST = os.path.join(BUILD, "pine_host.exe")
N = 240
SHOWS = ["Twinkle", "Snowfall", "Breathe", "Spiral garland", "Candle flicker", "Star ripple", "Fireflies",
         "Rising sparks", "Fill & drain", "Northern lights", "Fireworks", "Ornaments", "Soft radar", "Heartbeat",
         "Tinsel shimmer"]
STATES = ["morning", "work", "daydream", "doze", "sleep", "stir", "grumpy", "excited", "reminded", "react"]
EMO = ["energy", "focus", "mood", "annoy", "excite", "bond"]
# whole-Pine scenarios: (name, rng seed, seconds, Pine's clock, touch / light events "frame:what")
RUNS = [
    ("left alone, 1x clock", 12345, 120, 1, ""),
    ("left alone, 10x clock (demo)", 777, 180, 10, ""),
    ("touched a lot", 4242, 90, 10, "300:hover,500:tap,900:pet,1300:press,1700:squeeze,2100:tickle,2600:tap,"
                                    "3000:pet,3400:hover,3800:tap,4100:tap"),
    ("dark room, woken up", 99, 120, 30, "200:dark,2500:tap,3200:tap,3500:light,4000:squeeze,4500:tickle"),
]


def q_level(v):                     # v: Q12 ints -> 8-bit BCM level
    x = np.clip(v.astype(np.float64) / 4096, 0, 1)
    return np.floor(255 * x ** 2.2 + 0.5).astype(int)


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        sys.exit(f"{cmd[0]} failed:\n{r.stdout}{r.stderr}")


def stats(a, b):
    qa, qb = q_level(a), q_level(b)
    d = np.abs(qa - qb)
    return (d == 0).mean() * 100, (d <= 1).mean() * 100, int(d.max()), np.abs(a.astype(int) - b.astype(int)).mean() / 4096


def shows(seconds=30):
    frames = seconds * 50
    print(f"\nLight shows, {seconds} s each: share of LED-frames whose 8-bit level matches the simulator")
    print(f"{'show':<16}{'exact':>8}{'+-1':>8}{'max':>5}{'mean |dv|':>11}   first 5 s exact")
    worst = 100
    for k, name in enumerate(SHOWS):
        fa, fb = os.path.join(BUILD, f"js_show{k}.bin"), os.path.join(BUILD, f"c_show{k}.bin")
        run([NODE, os.path.join(HERE, "simref.js"), "show", str(k), "7", str(frames), fa])
        run([HOST, "show", str(k), "7", str(frames), fb])
        a = np.fromfile(fa, "<u2").reshape(frames, N)
        b = np.fromfile(fb, "<u2").reshape(frames, N)
        ex, ex1, mx, mean = stats(a, b)
        ex5 = stats(a[:250], b[:250])[0]
        worst = min(worst, ex1)
        print(f"{name:<16}{ex:7.2f}%{ex1:7.2f}%{mx:5d}{mean:11.5f}   {ex5:6.2f}%")
    return worst


def math_check():
    """Math.sin / Math.cos / % of the firmware (src/jsmath.c) against V8's, bit for bit."""
    rng = np.random.default_rng(1)
    t = np.cumsum(np.full(30000, 0.02))                     # the shows' clock: t += 0.02, as in the JS
    xs = np.concatenate([rng.uniform(-3000, 3000, 100000), rng.uniform(-10, 10, 100000), t * 1.4, t * 2.6, t * 0.23,
                         t * 2 * np.pi / 4.8, np.arange(-2000, 2000) * np.pi / 2, t])
    ys = np.concatenate([rng.uniform(0.1, 20, 200000), np.full(len(xs) - 200000, 1.25)])
    fi, fa, fb = (os.path.join(BUILD, n) for n in ("math_in.bin", "js_math.bin", "c_math.bin"))
    np.stack([xs, ys], 1).astype("<f8").tofile(fi)
    run([NODE, os.path.join(HERE, "simref.js"), "math", fi, fa])
    run([HOST, "math", fi, fb])
    a, b = np.fromfile(fa, "<u8").reshape(-1, 3), np.fromfile(fb, "<u8").reshape(-1, 3)
    same = (a == b).mean(0) * 100
    print(f"\nMath.sin {same[0]:.4f}%  Math.cos {same[1]:.4f}%  % {same[2]:.4f}% bit-identical ({len(xs)} arguments)")
    return same.min()


def runs():
    rec = N * 2 + 1 + 48
    dt = np.dtype([("mix", "<u2", N), ("state", "u1"), ("emo", "<f8", 6)])
    assert dt.itemsize == rec
    print("\nWhole Pine (brain + faces + scenes + transitions + touch), from power-on")
    for name, seed, secs, clock, ev in RUNS:
        frames = secs * 50
        fa, fb = os.path.join(BUILD, "js_run.bin"), os.path.join(BUILD, "c_run.bin")
        run([NODE, os.path.join(HERE, "simref.js"), "run", str(seed), str(frames), str(clock), ev, fa])
        run([HOST, "run", str(seed), str(frames), str(clock), ev, fb])
        a, b = np.fromfile(fa, dt), np.fromfile(fb, dt)
        same_state = a["state"] == b["state"]
        div = int(np.argmin(same_state)) if not same_state.all() else None
        upto = div if div is not None else frames
        ex, ex1, mx, mean = stats(a["mix"][:upto], b["mix"][:upto])
        emo = np.abs(a["emo"][:upto] - b["emo"][:upto]).max()
        path = []
        for s in a["state"][:upto]:
            if not path or path[-1] != STATES[s]:
                path.append(STATES[s])
        print(f"- {name} ({secs} s, seed {seed}, clock {clock}x)")
        print(f"  states: {' > '.join(path[:14])}{' ...' if len(path) > 14 else ''}")
        if div is None:
            print(f"  state machine identical for all {frames} frames; emotions max diff {emo:.2e}")
        else:
            print(f"  STATE DIVERGES at frame {div} ({div / 50:.2f} s): sim {STATES[a['state'][div]]}, "
                  f"firmware {STATES[b['state'][div]]}")
        print(f"  LED levels up to there: {ex:.2f}% exact, {ex1:.2f}% within 1, max diff {mx}, mean |dv| {mean:.5f}")


if __name__ == "__main__":
    what = sys.argv[1:] or ["math", "shows", "runs"]
    run([sys.executable, os.path.join(ROOT, "build.py"), "host"])
    if "math" in what:
        math_check()
    if "shows" in what:
        shows()
    if "runs" in what:
        runs()
