"""Touch-field experiment (python build.py touchfield, flashed): speaks where to touch, records the 7 LED lines
L0-L6 and the star, and prints each line's change per spot (negative % = more capacitance). No LED is driven.

  python test/touchfield.py
"""
import re
import statistics
import subprocess
import sys
import time

from pyocd.core.helpers import ConnectHelper

NM = r"C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 rel1/bin/arm-none-eabi-nm.exe"
SAY = r"C:/Users/marec/.claude/skills/live-debug/say.py"
STEPS = [("rest", 5, "Touch field test. Hands away from the tree."),
         ("top", 6, "Put a fingertip on the top of the tree, just under the star."),
         ("gap", 3, "Let go."),
         ("left", 6, "Now the left side of the tree, halfway down."),
         ("gap", 3, "Let go."),
         ("right", 6, "Now the right side, halfway down."),
         ("gap", 3, "Let go."),
         ("middle", 6, "Now the middle of the tree."),
         ("gap", 3, "Let go."),
         ("bottom", 6, "Now the bottom of the tree, just above the trunk."),
         ("gap", 3, "Let go."),
         ("palm", 6, "Now cover the whole front with your palm."),
         ("rest2", 4, "Thank you, hands away.")]
addr = {m.group(2): int(m.group(1), 16) for m in re.finditer(
    r"([0-9a-f]{8}) [bBdD] (\w+)", subprocess.run([NM, "build/touchfield.elf"], capture_output=True, text=True).stdout)}
data = {}
with ConnectHelper.session_with_chosen_probe(options={"connect_mode": "attach", "resume_on_disconnect": True},
                                             target_override="py32f003x8") as s:
    t = s.target
    for label, secs, text in STEPS:
        print(f"[{label}] {text}", flush=True)
        subprocess.Popen([sys.executable, SAY, text])
        rows, t0 = [], time.time()
        while time.time() - t0 < secs:
            rows.append(t.read_memory_block32(addr["field"], 8))
            time.sleep(0.05)
        rows = rows[len(rows) // 3:]                     # skip the reaction time
        data.setdefault(label, []).extend(rows)
subprocess.Popen([sys.executable, SAY, "Done."])
base = [statistics.median(r[k] for r in data["rest"] + data["rest2"]) for k in range(8)]
noise = [statistics.pstdev(r[k] for r in data["rest"]) / base[k] * 100 for k in range(8)]
print("\nchange from rest, % (L7 = the star)")
print(f"{'':8}" + "".join(f"{'L' + str(k):>8}" for k in range(8)))
print(f"{'noise':8}" + "".join(f"{n:8.2f}" for n in noise))
for label in ("top", "left", "right", "middle", "bottom", "palm"):
    med = [statistics.median(r[k] for r in data[label]) for k in range(8)]
    print(f"{label:8}" + "".join(f"{(m / b - 1) * 100:8.2f}" for m, b in zip(med, base)))
