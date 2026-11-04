"""Touch-field experiment, round 2 (python build.py touchfield): flashes the recorder, speaks where to touch while
the board records on its own (no debugger traffic), then halts it and reads everything out. No LED is driven.
Prints, per method and line, the change per spot in units of that line's noise at rest (|z| > 3 = clearly seen).

  python test/touchfield.py
"""
import re
import statistics
import subprocess
import sys
import time

from pyocd.core.helpers import ConnectHelper

HERE = sys.path[0]
NM = r"C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 rel1/bin/arm-none-eabi-nm.exe"
SAY = r"C:/Users/marec/.claude/skills/live-debug/say.py"
ROUND = 0.8
STEPS = [
    ("rest", 0, 8, "Touch field test three. Hands away from the tree."),
    ("top", 8, 16, "Fingertip on the top tip, just under the star, and hold."),
    ("gap", 16, 18, "Let go."),
    ("up-left", 18, 26, "Fingertip on the upper left, and hold."),
    ("gap", 26, 28, "Let go."),
    ("up-right", 28, 36, "Fingertip on the upper right, and hold."),
    ("gap", 36, 38, "Let go."),
    ("mid-left", 38, 46, "Fingertip on the middle left, and hold."),
    ("gap", 46, 48, "Let go."),
    ("centre", 48, 56, "Fingertip on the very centre, and hold."),
    ("gap", 56, 58, "Let go."),
    ("mid-right", 58, 66, "Fingertip on the middle right, and hold."),
    ("gap", 66, 68, "Let go."),
    ("low-left", 68, 76, "Fingertip on the lower left, and hold."),
    ("gap", 76, 78, "Let go."),
    ("low-mid", 78, 86, "Fingertip on the lower middle, above the trunk, and hold."),
    ("gap", 86, 88, "Let go."),
    ("low-right", 88, 96, "Fingertip on the lower right, and hold."),
    ("gap", 96, 98, "Let go."),
]
run = lambda c: subprocess.run(c, capture_output=True, text=True)
r = run([sys.executable, f"{HERE}/../../flash/flash.py", f"{HERE}/../build/touchfield.hex"])
if "running" not in r.stdout:
    sys.exit(r.stdout + r.stderr)
t0 = time.time()
for label, a, b, text in STEPS:
    while time.time() - t0 < a:
        time.sleep(0.02)
    print(f"[{label}] {text}", flush=True)
    subprocess.Popen([sys.executable, SAY, text])
while time.time() - t0 < 101:
    time.sleep(0.1)
subprocess.Popen([sys.executable, SAY, "Done, thank you. Hands away."])
addr = {m.group(2): int(m.group(1), 16) for m in re.finditer(
    r"([0-9a-f]{8}) [bBdD] (\w+)", run([NM, f"{HERE}/../build/touchfield.elf"]).stdout)}
with ConnectHelper.session_with_chosen_probe(options={"connect_mode": "attach"}, target_override="py32f003x8") as s:
    t = s.target
    t.halt()
    n = t.read32(addr["nrec"])
    raw = bytes(t.read_memory_block8(addr["rec"], 125 * 49))
    t.resume()
LINES = ["L0", "L1", "L2", "L3", "L4", "L5", "L6", "L7", "L10", "L11", "L12", "L13", "L14", "L15"]
rec = [(list(raw[i * 49: i * 49 + 42]), list(raw[i * 49 + 42: i * 49 + 49])) for i in range(n)]
print(f"{n} rounds recorded")
rounds = lambda a, b: [i for i in range(n) if a + 2 <= i * ROUND and (i + 1) * ROUND <= b]
rest = rounds(0, 8)
def table(name, cols, get):
    base = [statistics.mean(get(i)[c] for i in rest) for c in range(len(cols))]
    sd = [max(statistics.pstdev(get(i)[c] for i in rest), 2.0) for c in range(len(cols))]
    print(f"{name}: change from rest in noise units (rest level, noise below)")
    print(f"{'':10}" + "".join(f"{c:>6}" for c in cols))
    print(f"{'level':10}" + "".join(f"{b:6.0f}" for b in base))
    print(f"{'noise':10}" + "".join(f"{x:6.1f}" for x in sd))
    for label, a, b, _ in STEPS:
        if label in ("rest", "gap"):
            continue
        med = [statistics.median(get(i)[c] for i in rounds(a, b)) for c in range(len(cols))]
        print(f"{label:10}" + "".join(f"{(x - b0) / s0:6.0f}" for x, b0, s0 in zip(med, base, sd)))
for w, ms in enumerate((2, 5, 20)):
    table(f"digital, {ms} ms listening", LINES, lambda i, w=w: rec[i][0][w * 14:(w + 1) * 14])
table("ADC spread, 20 ms", LINES[:7], lambda i: rec[i][1])
