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
ROUND = 0.6
STEPS = [("rest", 0, 8, "Touch field test. Hands away from the tree."),
         ("top", 8, 16, "Fingertip on the top of the tree, just under the star, and hold."),
         ("gap", 16, 20, "Let go."),
         ("left", 20, 28, "Fingertip on the left side, halfway down, and hold."),
         ("gap", 28, 32, "Let go."),
         ("right", 32, 40, "Fingertip on the right side, halfway down, and hold."),
         ("gap", 40, 44, "Let go."),
         ("middle", 44, 52, "Fingertip in the middle of the tree, and hold."),
         ("gap", 52, 56, "Let go."),
         ("bottom", 56, 64, "Fingertip at the bottom, just above the trunk, and hold."),
         ("gap", 64, 66, "Let go."),
         ("palm", 66, 72, "Now cover the whole front with your palm.")]
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
while time.time() - t0 < 74:
    time.sleep(0.1)
subprocess.Popen([sys.executable, SAY, "Done, thank you. Hands away."])
addr = {m.group(2): int(m.group(1), 16) for m in re.finditer(
    r"([0-9a-f]{8}) [bBdD] (\w+)", run([NM, f"{HERE}/../build/touchfield.elf"]).stdout)}
with ConnectHelper.session_with_chosen_probe(options={"connect_mode": "attach"}, target_override="py32f003x8") as s:
    t = s.target
    t.halt()
    n = t.read32(addr["nrec"])
    raw = t.read_memory_block32(addr["rec"], 120 * 3 * 8 // 2)
    t.resume()
vals = [w >> (16 * h) & 0xFFFF for w in raw for h in (0, 1)]
rec = [[[vals[(i * 3 + m) * 8 + k] for k in range(8)] for m in range(3)] for i in range(n)]
print(f"\n{n} rounds recorded")
rounds = lambda a, b: [i for i in range(n) if a + 2 <= i * ROUND and (i + 1) * ROUND <= b]
rest = rounds(0, 8)
for m, name in enumerate(("A charge sharing", "B timed RC charge", "C hum spread")):
    base = [statistics.median(rec[i][m][k] for i in rest) for k in range(8)]
    sd = [max(statistics.pstdev(rec[i][m][k] for i in rest), 0.5) for k in range(8)]
    print(f"\n{name}: change from rest in noise units (L7 = the star); rest level and noise:")
    print(f"{'':8}" + "".join(f"{'L' + str(k):>8}" for k in range(8)))
    print(f"{'level':8}" + "".join(f"{b:8.0f}" for b in base))
    print(f"{'noise':8}" + "".join(f"{x:8.1f}" for x in sd))
    for label, a, b, _ in STEPS:
        if label in ("rest", "gap"):
            continue
        med = [statistics.median(rec[i][m][k] for i in rounds(a, b)) for k in range(8)]
        print(f"{label:8}" + "".join(f"{(x - b0) / s0:8.1f}" for x, b0, s0 in zip(med, base, sd)))
