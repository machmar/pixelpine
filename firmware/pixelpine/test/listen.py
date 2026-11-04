"""Touch-field listening while Pine runs (python build.py listen): flashes it, speaks where to touch, waits until
the firmware has switched every LED off by itself (90 s), then halts and reads the per-3-second hum spread of the
lines L0-L6. Prints each spot's change from rest in noise units, per gap setting.

  python test/listen.py
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
SPOTS = [("rest", "hands away from the tree"), ("low-mid", "fingertip on the lower middle, above the trunk, and hold"),
         ("mid-right", "fingertip on the middle right"), ("up-right", "fingertip on the upper right"),
         ("low-left", "fingertip on the lower left")]
STEPS = []
for p, (t0, intro) in enumerate(((0, "First setting: short gaps. Watch for flicker. "),
                                 (45, "Second setting: longer gaps, less often. Compare the flicker. "))):
    for k, (lab, txt) in enumerate(SPOTS):
        STEPS.append((p, lab, t0 + 9 * k, t0 + 9 * k + 9, (intro if k == 0 else "") + txt.capitalize() + "."))
run = lambda c: subprocess.run(c, capture_output=True, text=True)
r = run([sys.executable, f"{HERE}/../../flash/flash.py", f"{HERE}/../build/listen.hex"])
if "running" not in r.stdout:
    sys.exit(r.stdout + r.stderr)
start = time.time()
for p, lab, a, b, text in STEPS:
    while time.time() - start < a:
        time.sleep(0.02)
    print(f"[{lab}] {text}", flush=True)
    subprocess.Popen([sys.executable, SAY, text])
while time.time() - start < 93:
    time.sleep(0.1)
subprocess.Popen([sys.executable, SAY, "Done. The LEDs are off now, that is expected."])
addr = {m.group(2): int(m.group(1), 16) for m in re.finditer(
    r"([0-9a-f]{8}) [bBdD] (\w+)", run([NM, f"{HERE}/../build/listen.elf"]).stdout)}
with ConnectHelper.session_with_chosen_probe(options={"connect_mode": "attach"}, target_override="py32f003x8") as s:
    t = s.target
    if not t.read32(addr["ldone"]):
        sys.exit("firmware hasn't finished: not halting it with LEDs running")
    t.halt()
    raw = bytes(t.read_memory_block8(addr["lbin"], 31 * 7))
    t.resume()
bins = [list(raw[i * 7:(i + 1) * 7]) for i in range(31)]
inside = lambda a, b: [i for i in range(31) if a + 3 <= 3 * i and 3 * i + 3 <= b]    # skip the first 3 s
for p, name in enumerate(("4 ms every 100 ms", "8 ms every 250 ms")):
    rest = [i for q, lab, a, b, _ in STEPS if q == p and lab == "rest" for i in inside(a, b)]
    base = [statistics.mean(bins[i][k] for i in rest) for k in range(7)]
    sd = [max(statistics.pstdev([bins[i][k] for i in rest]) if len(rest) > 1 else 0, 1.0) for k in range(7)]
    print(f"\ngaps of {name}: change from rest in noise units; rest level (spread / 16)")
    print(f"{'':10}" + "".join(f"{'L' + str(k):>6}" for k in range(7)))
    print(f"{'level':10}" + "".join(f"{x:6.0f}" for x in base))
    for q, lab, a, b, _ in STEPS:
        if q != p or lab == "rest":
            continue
        m = [statistics.mean(bins[i][k] for i in inside(a, b)) for k in range(7)]
        print(f"{lab:10}" + "".join(f"{(x - b0) / s0:6.0f}" for x, b0, s0 in zip(m, base, sd)))
