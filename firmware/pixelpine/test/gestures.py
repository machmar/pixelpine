"""Guided gesture test on the calib build: speaks each gesture, then reports which gestures the firmware's
classifier fired in that window and the peak touch level. python test/gestures.py"""
import re
import subprocess
import sys
import time

from pyocd.core.helpers import ConnectHelper

ELF = "build/calib.elf"
NM = r"C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 rel1/bin/arm-none-eabi-nm.exe"
SAY = r"C:/Users/marec/.claude/skills/live-debug/say.py"
GESTURES = ["hover", "tap", "press", "squeeze", "tickle", "pet start", "pet end"]
STEPS = [
    ("tap", 6, "Gesture test. Tap the star once, quickly."),
    ("tap", 6, "Tap it once more."),
    ("pet", 8, "Now rest your finger on the star for three seconds, then let go."),
    ("press", 7, "Now press firmly with a flattened fingertip for one second, then let go."),
    ("squeeze", 7, "Now pinch the star front and back for one second, then let go."),
    ("tickle", 7, "Now tap three times, quickly."),
    ("none", 4, "Done, thank you."),
]
syms = {m.group(2): int(m.group(1), 16) for m in
        re.finditer(r"([0-9a-f]{8}) [bBdD] (\w+)", subprocess.run([NM, ELF], capture_output=True, text=True).stdout)}
results = []
with ConnectHelper.session_with_chosen_probe(target_override="py32f003x8",
                                             options={"connect_mode": "attach", "resume_on_disconnect": True}) as s:
    t = s.target
    ev0 = t.read32(syms["touch_events"])
    for want, secs, text in STEPS:
        print(f"[{want}] {text}", flush=True)
        subprocess.Popen([sys.executable, SAY, text])
        seen, peak, t0 = [], 0.0, time.time()
        while time.time() - t0 < secs:
            ev = t.read32(syms["touch_events"])
            if ev != ev0:
                ev0 = ev
                seen.append(GESTURES[t.read8(syms["touch_event_last"])])
            lvl = t.read32(syms["touch_level_last"]) / 4096
            peak = max(peak, lvl if lvl < 100 else 0)
            time.sleep(0.03)
        print(f"   fired: {seen or 'nothing'}   peak level {peak:.2f}", flush=True)
        results.append((want, seen, peak))
ok = sum(1 for w, seen, _ in results if w != "none" and (w in seen or (w == "pet" and "pet start" in seen)))
print(f"\n{ok} of {len(results) - 1} gestures recognised")
