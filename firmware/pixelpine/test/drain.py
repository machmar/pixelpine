"""Touch-field experiment 5: drain to 0 V, then listen (python build.py drain; see the DRAIN block in hw/main.c).
Step 1: `python test/drain.py flash` (SWD wires on); the tree stays dark 10 s: unplug SWDIO / SWCLK. Then a wave
(hands off for 10 s), then blinks 9 spots in turn: touch and hold each until the next one blinks. ~100 s, then dark.
Step 2: when the tree has gone dark, plug SWDIO / SWCLK back in and run `python test/drain.py read` (one attach,
no halt). Keep the probe's RESET wire unplugged during the run: connect attempts can pulse it.
"""
import math
import re
import subprocess
import sys

from pyocd.core.helpers import ConnectHelper

HERE = sys.path[0]
NM = r"C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 rel1/bin/arm-none-eabi-nm.exe"
STEPS = ["rest", "top", "up-left", "up-right", "mid-left", "centre", "mid-right", "low-left", "low-mid", "low-right"]
SETS = ["A no drain, 0.7 ms", "B drain, 20 us", "C drain, 0.2 ms", "D drain, 0.7 ms", "E drain, 2 ms"]
NSTEP, NSET, NCH = 10, 5, 9
run = lambda c: subprocess.run(c, capture_output=True, text=True)

if sys.argv[1:] == ["flash"]:
    r = run([sys.executable, f"{HERE}/../build.py", "drain"])
    r = run([sys.executable, f"{HERE}/../../flash/flash.py", f"{HERE}/../build/drain.hex"])
    print(r.stdout.strip().splitlines()[-1] if "running" in r.stdout else r.stdout + r.stderr)
    sys.exit()

addr = {m.group(2): int(m.group(1), 16) for m in re.finditer(
    r"([0-9a-f]{8}) [bBdDV] (\w+)", run([NM, f"{HERE}/../build/drain.elf"]).stdout)}
for attempt in range(1):                         # one try only: polling while the run is on resets the chip through the
                                                 # probe's RESET wire (seen in experiments 4 and 5), so read after it ends
    try:
        with ConnectHelper.session_with_chosen_probe(options={"connect_mode": "attach"},
                                                     target_override="py32f003x8") as s:
            t = s.target
            csr, crash = t.read32(0x40021060), t.read_memory_block32(addr["crash_rec"], 4)
            done, frame = t.read32(addr["ddone"]), t.read32(addr["dframe"])
            if done:
                dsum = t.read_memory_block32(addr["dsum"], NSTEP * NSET * NCH)
                dsq = t.read_memory_block32(addr["dsq"], NSTEP * NSET * 7)
                cb = t.read_memory_block8(addr["dcnt"], NSTEP * NSET * 2)
                break
            print(f"running, frame {frame} of 5000")
    except Exception as e:                       # noqa: BLE001 - probe not wired yet
        print("waiting for the SWD wires...", str(e)[:60])
else:
    sys.exit("couldn't read")

flags = [n for b, n in ((29, "watchdog"), (28, "software"), (27, "power-on"), (26, "NRST pin"), (25, "brown-out"))
         if csr >> b & 1]
print("last reset reasons:", ", ".join(flags) or "none")
if crash[0] == 0xC4A5C4A5:
    print(f"crash record: {crash[3]} crash(es), last at PC 0x{crash[1]:08X}")
cnt = [[cb[2 * (s * NSET + k)] | cb[2 * (s * NSET + k) + 1] << 8 for k in range(NSET)] for s in range(NSTEP)]
mean = lambda s, k, c: dsum[(s * NSET + k) * NCH + c] / max(cnt[s][k], 1)


def sd(s, k, c):
    n = max(cnt[s][k], 1)
    m4 = mean(s, k, c) / 4
    return 4 * math.sqrt(max(dsq[(s * NSET + k) * 7 + c] / n - m4 * m4, 0))


for k, name in enumerate(SETS):
    print(f"\nsetting {name}: mean ADC count (0-4095) on L0-L6, comparator > 0.3 V share on L1 / L11")
    print(f"{'':10}" + "".join(f"{'L' + str(c):>7}" for c in range(7)) + "  cmpL1 cmpL11  gaps   best line (z vs rest)")
    for s, step in enumerate(STEPS):
        row = f"{step:10}" + "".join(f"{mean(s, k, c):7.0f}" for c in range(7))
        row += f"  {100 * mean(s, k, 7):4.0f}% {100 * mean(s, k, 8):5.0f}%  {cnt[s][k]:4d}"
        if s:
            z = [(mean(s, k, c) - mean(0, k, c)) / max(sd(0, k, c), 8) for c in range(7)]
            b = max(range(7), key=lambda c: z[c])
            row += f"   L{b} z={z[b]:.1f} (touched sd {sd(s, k, b):.0f})"
        print(row)
    print(f"{'rest sd':10}" + "".join(f"{sd(0, k, c):7.0f}" for c in range(7)))
