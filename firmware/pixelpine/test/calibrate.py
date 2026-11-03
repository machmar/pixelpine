"""Guided sensor calibration on the calib build (python build.py calib): speaks each step, records the touch and
light readings over SWD, and prints the median per step with the suggested SENSE_TOUCH_UNIT / SENSE_LIGHT_DARK.

  python test/calibrate.py [--quiet]
"""
import re
import statistics
import subprocess
import sys
import time

from pyocd.core.helpers import ConnectHelper

ELF = "build/calib.elf"
NM = r"C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 rel1/bin/arm-none-eabi-nm.exe"
SAY = r"C:/Users/marec/.claude/skills/live-debug/say.py"
STEPS = [  # (label, seconds, spoken instruction)
    ("idle", 8, "Touch calibration. Hands away from the star, please."),
    ("touch", 10, "Rest one finger lightly on the front of the star, and keep it there."),
    ("gap1", 5, "Let go."),
    ("press", 10, "Now press the star firmly with a flattened fingertip, and hold."),
    ("gap2", 5, "Let go."),
    ("squeeze", 10, "Now pinch the star between two fingers, front and back, and hold."),
    ("gap3", 5, "Let go."),
    ("hover", 10, "Now hold a finger about one centimetre above the star, without touching."),
    ("gap4", 4, "Thank you, hands away."),
]
syms = {m.group(2): int(m.group(1), 16) for m in
        re.finditer(r"([0-9a-f]{8}) [bBdD] (\w+)", subprocess.run([NM, ELF], capture_output=True, text=True).stdout)}
quiet = "--quiet" in sys.argv
data = {}
trace = []
t00 = time.time()
with ConnectHelper.session_with_chosen_probe(target_override="py32f003x8",
                                             options={"connect_mode": "attach", "resume_on_disconnect": True}) as s:
    t = s.target
    for label, secs, text in STEPS:
        print(f"[{label}] {text}", flush=True)
        if not quiet:
            subprocess.Popen([sys.executable, SAY, text])
        raws, bases, lights = [], [], []
        t0 = time.time()
        while time.time() - t0 < secs:
            raws.append(t.read16(syms["touch_raw_last"]))
            bases.append(t.read32(syms["touch_base_last"]))
            lights.append(0)
            time.sleep(0.05)
        trace.extend((label, round(time.time() - t00, 2), r) for r in raws[-len(raws):])
        tail = slice(len(raws) // 3, None)          # skip the reaction time at the start of each step
        data[label] = (statistics.median(raws[tail]), statistics.median(bases[tail]), statistics.median(lights[tail]),
                       min(raws[tail]), max(raws[tail]))
        r, b, l, lo, hi = data[label]
        print(f"   raw median {r:7.0f} (min {lo}, max {hi})  baseline {b:7.0f}  drop {b - r:6.0f}  light {l:6.0f}", flush=True)
if not quiet:
    subprocess.Popen([sys.executable, SAY, "Calibration done. Thank you."])
idle = data["idle"][0]
drops = {k: idle - data[k][0] for k in ("touch", "press", "squeeze", "hover")}
print("\ndrops from idle:", {k: round(v) for k, v in drops.items()})
if drops["touch"] > 20:
    # the simulator's recorded gestures: light touch 0.5, press 0.82, squeeze 1.32, hover 0.22
    print(f"suggested SENSE_TOUCH_UNIT = {drops['touch'] / 0.5:.0f} (touch -> 0.5); press would read "
          f"{drops['press'] / (drops['touch'] / 0.5):.2f}, squeeze {drops['squeeze'] / (drops['touch'] / 0.5):.2f}, "
          f"hover {drops['hover'] / (drops['touch'] / 0.5):.2f}")
with open("build/calib_trace.csv", "w") as f:
    f.write("step,t,raw\n" + "".join(f"{a},{b},{c}\n" for a, b, c in trace))
