"""Read the touch recorder (python build.py touchrec). Flash it, unplug SWDIO/SWCLK, do the gestures; after 180 s
the tree goes dark and PA13/PA14 are SWD again: plug the probe back in (no RESET wire needed), then run this once.
It attaches without a reset or halt. python test/touchrec.py"""
import re
import subprocess

from pyocd.core.helpers import ConnectHelper

ELF = "build/touchrec.elf"
NM = r"C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 rel1/bin/arm-none-eabi-nm.exe"
G = ["hover", "tap", "press", "squeeze", "tickle", "pet start", "pet end"]
syms = {m.group(3): (int(m.group(1), 16), int(m.group(2), 16)) for m in
        re.finditer(r"([0-9a-f]{8}) ([0-9a-f]{8}) [bBdD] (\w+)",
                    subprocess.run([NM, "-S", ELF], capture_output=True, text=True).stdout)}
with ConnectHelper.session_with_chosen_probe(target_override="py32f003x8",
                                             options={"connect_mode": "attach", "resume_on_disconnect": True}) as s:
    t = s.target
    rd = lambda n: bytes(t.read_memory_block8(*syms[n]))
    frozen, n = rd("rec_frozen")[0], rd("rec_n")[0]
    ep = rd("rec_ep")
    gpio = int.from_bytes(rd("rec_gpio_cycles"), "little")
    late = int.from_bytes(rd("frames_late"), "little")
    worst = int.from_bytes(rd("frame_cycles_max"), "little")
    fno = int.from_bytes(rd("frame_no"), "little")
print(f"frozen {frozen}, frames {fno}, late frames {late}, worst frame {worst / 24000:.1f} ms, "
      f"64 GPIO stores = {gpio} cycles ({'IOPORT, 1 cycle each' if gpio < 100 else 'AHB, 2 cycles each'})")
fmt = lambda f: "-" if f == 255 else f"{f * 20} ms"
print(f"{'#':>2} {'t':>5} {'held':>7} {'peak':>5} {'press@':>7} {'squeeze@':>8}  fired")
for k in range(min(n, len(ep) // 11)):
    t0, dur, peak, fp, fs = ep[k * 11: k * 11 + 5]
    evg, evf = ep[k * 11 + 5: k * 11 + 8], ep[k * 11 + 8: k * 11 + 11]
    fired = ", ".join(f"{G[g - 1]} @{f * 20} ms" for g, f in zip(evg, evf) if g) or "nothing"
    print(f"{k + 1:>2} {t0:>4}s {dur * 20:>5} ms {peak / 128:5.2f} {fmt(fp):>7} {fmt(fs):>8}  {fired}")
