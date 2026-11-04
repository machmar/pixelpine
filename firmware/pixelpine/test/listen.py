"""Touch-field listening while Pine runs, paced by the tree (python build.py listen; see the LISTEN block in
hw/main.c). Step 1: `python test/listen.py flash` (SWD wires on), then unplug SWDIO / SWCLK. The tree shows "1",
runs Pine (hands off), blinks 4 spots in turn (touch and hold each), then "2" and the same, then goes dark.
Step 2: plug SWDIO / SWCLK back in and run `python test/listen.py read`: it reads the results without halting.
Also prints the chip's last reset reason and crash record.
"""
import re
import subprocess
import sys

from pyocd.core.helpers import ConnectHelper

HERE = sys.path[0]
NM = r"C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 rel1/bin/arm-none-eabi-nm.exe"
STEPS = ["rest", "low-mid", "mid-right", "up-right", "low-left"]
run = lambda c: subprocess.run(c, capture_output=True, text=True)

if sys.argv[1:] == ["flash"]:
    r = run([sys.executable, f"{HERE}/../../flash/flash.py", f"{HERE}/../build/listen.hex"])
    print(r.stdout.strip().splitlines()[-1] if "running" in r.stdout else r.stdout + r.stderr)
    sys.exit()
addr = {m.group(2): int(m.group(1), 16) for m in re.finditer(
    r"([0-9a-f]{8}) [bBdDV] (\w+)", run([NM, f"{HERE}/../build/listen.elf"]).stdout)}
with ConnectHelper.session_with_chosen_probe(options={"connect_mode": "attach"}, target_override="py32f003x8") as s:
    t = s.target
    csr = t.read32(0x40021060)
    crash = t.read_memory_block32(addr["crash_rec"], 4)
    done = t.read32(addr["ldone"])
    frame = t.read32(addr["lframe"])
    raw = t.read_memory_block8(addr["lsum"], 2 * 5 * 7 * 2) if done else None
    cnt = t.read_memory_block8(addr["lcnt"], 2 * 5 * 2) if done else None
flags = [n for b, n in ((31, "low-power"), (30, "window wdg"), (29, "watchdog"), (28, "software"), (27, "power-on"),
                        (26, "NRST pin"), (25, "brown-out")) if csr >> b & 1]
print("last reset reasons:", ", ".join(flags) or "none")
if crash[0] == 0xC4A5C4A5:
    print(f"crash record: {crash[3]} crash(es), last at PC 0x{crash[1]:08X} (LR 0x{crash[2]:08X})")
if not done:
    sys.exit(f"not finished yet (frame {frame} of 5000)")
w16 = lambda b, i: b[2 * i] | b[2 * i + 1] << 8
for p, name in enumerate(("4 ms gaps every 100 ms", "8 ms gaps every 250 ms")):
    print(f"setting {p + 1} ({name}): mean hum spread per gap (/16) on lines L0-L6, gaps measured")
    rest = None
    for k, step in enumerate(STEPS):
        n = w16(cnt, p * 5 + k)
        mean = [w16(raw, (p * 5 + k) * 7 + j) / max(n, 1) for j in range(7)]
        rest = rest or mean
        line = f"{step:10}" + "".join(f"{m:7.1f}" for m in mean) + f"   gaps {n:3d}"
        if k:
            best = max(range(7), key=lambda j: mean[j] - rest[j])
            line += f"   strongest L{best} (+{mean[best] - rest[best]:.1f})"
        print(line)
