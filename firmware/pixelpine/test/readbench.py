"""Read the bench build's results over SWD (see hw/main.c, BENCH)."""
import re, subprocess, sys
ELF = "build/bench.elf"
nm = subprocess.run([r"C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 rel1/bin/arm-none-eabi-nm.exe", ELF],
                    capture_output=True, text=True).stdout
addr = {m.group(2): int(m.group(1), 16) for m in re.finditer(r"([0-9a-f]{8}) B (bench_\w+)", nm)}
cmd = [sys.executable, "-m", "pyocd", "cmd", "-t", "py32f003x8", "-O", "connect_mode=attach"]
for name, n in (("bench_eng", 4), ("bench_prim", 8), ("bench_part", 4), ("bench_show", 15), ("bench_done", 1), ("bench_max", 15)):
    cmd += ["-c", f"read32 {addr[name]:#x} {n * 4}"]
r = subprocess.run(cmd, capture_output=True, text=True)
out = r.stdout + r.stderr
words = [int(w, 16) for l in out.splitlines() if re.match(r"^[0-9a-f]{8}:", l) for w in l.split(":", 1)[1].split("|")[0].split()]
eng, prim, words = words[:4], words[4:12], words[12:]
if len(words) < 35:
    sys.exit(out)
names = ["Twinkle", "Snowfall", "Breathe", "Garland", "Candle", "Ripple", "Fireflies", "Sparks", "Fill", "Aurora",
         "Fireworks", "Ornaments", "Radar", "Heart", "Tinsel"]
ms = lambda c: c / 24000
print("cycles per call: noise2 %.0f, qsin %.0f, jsin %.0f, noise2r %.0f, qgauss %.0f, double mul+add %.0f, splat r=2.65 %.0f, qm loop %.0f" % tuple(p / 1000 for p in prim))
print("engine worst: step cur %.2f ms, step to %.2f ms, transition params %.2f ms, face overlay %.2f ms" % tuple(e / 24000 for e in eng))
print(f"gamma8 x240: {ms(words[0]):.2f} ms   show_px x240: {ms(words[1]):.2f} ms   passes done: {words[19]}")
print(f"worst engine_tick {ms(words[2]):.2f} ms, worst output (levels + masks) {ms(words[3]):.2f} ms")
print(f"{'show':<10}{'frame() alone':>14}{'whole frame, jelly + overlay':>30}")
for k, n in enumerate(names):
    print(f"{n:<10}{ms(words[4 + k]):11.2f} ms{ms(words[20 + k]):27.2f} ms")
