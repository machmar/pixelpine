"""Live sensor monitor for the calib build (python build.py calib): prints the touch reading, its baseline, the
touch level in the simulator's units, the light reading, Pine's state and the frame timing, ten times a second.

  python test/sensemon.py [seconds] [--log file]
"""
import re
import subprocess
import sys
import time

from pyocd.core.helpers import ConnectHelper

ELF = "build/calib.elf"
NM = r"C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 rel1/bin/arm-none-eabi-nm.exe"
GESTURES = ["hover", "tap", "press", "squeeze", "tickle", "pet start", "pet end"]
STATES = ["morning", "work", "daydream", "doze", "sleep", "stir", "grumpy", "excited", "reminded", "react"]
syms = {m.group(3): (int(m.group(1), 16), int(m.group(2), 16)) for m in
        re.finditer(r"([0-9a-f]{8}) ([0-9a-f]{8}) [bBdD] (\w+)", subprocess.run([NM, "-S", ELF], capture_output=True, text=True).stdout)}
secs = float(sys.argv[1]) if len(sys.argv) > 1 and not sys.argv[1].startswith("-") else 1e9
log = open(sys.argv[sys.argv.index("--log") + 1], "a") if "--log" in sys.argv else None
unit = 240
with ConnectHelper.session_with_chosen_probe(target_override="py32f003x8",
                                             options={"connect_mode": "attach", "resume_on_disconnect": True}) as s:
    t = s.target
    r16 = lambda n: t.read16(syms[n][0])
    r32 = lambda n: t.read32(syms[n][0])
    t0 = time.time()
    ev0 = r32("touch_events")
    while time.time() - t0 < secs:
        ev = r32("touch_events")
        if ev != ev0:
            ev0 = ev
            g = GESTURES[t.read8(syms["touch_event_last"][0])]
            print(f"{time.time() - t0:7.1f}s  >>> gesture: {g}", flush=True)
            if log:
                log.write(f"{time.time() - t0:7.1f}s  >>> gesture: {g}\n"); log.flush()
        raw, base = r16("touch_raw_last"), r32("touch_base_last")
        st = t.read8(syms["brain"][0] + 6 * 8)                      # brain_t: 6 doubles, then state
        lvl = t.read32(syms["touch_level_last"][0]) / 4096
        line = (f"{time.time() - t0:7.1f}s  touch raw {raw:5d} [{r16('touch_min')}..{r16('touch_max')}] base {base:5d} drop {base - raw:5d} (~{lvl:4.2f})  "
                f"state {STATES[st] if st < len(STATES) else st:<9} late {r32('frames_late')}  "
                f"worst {r32('frame_cycles_max') / 24000:5.1f} ms")
        print(line, flush=True)
        if log:
            log.write(line + "\n"); log.flush()
        time.sleep(0.1)
