"""Build the sizing prototype and report flash / RAM / stack for each MCU.

  python build.py              both targets (a target whose compiler isn't installed is skipped)
  python build.py py32f003     the product MCU  - Arm GNU Toolchain (arm-none-eabi-gcc)
  python build.py ch32v003     the original MCU - xPack riscv-none-elf-gcc, kept for the comparison

The shared code (shows, faces, scenes, brain, tables) is identical for both; only the drivers in
py32f003/ and ch32v003/ (startup, scan ISR, touch ADC, flash store, linker script) differ.
"""
import glob
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
LOCAL = os.environ.get("LOCALAPPDATA", "")
COMMON = ["shows.c", "face.c", "scene.c", "brain.c", "tables.c"]
DRIVERS = ["main.c", "scan.c", "touch.c", "store.c"]
CFLAGS = ["-Os", "-ffunction-sections", "-fdata-sections", "-ffreestanding", "-fno-common", "-Wall", "-Wextra",
          "-Wno-unused-parameter", "-Wdouble-promotion", "-std=gnu2x"] + os.environ.get("EXTRA", "").split()
FLASH_SECTIONS = (".init", ".vector", ".isr_vector", ".text", ".ARM.exidx", ".ARM.extab")

TARGETS = {
    "py32f003": dict(
        title="PY32F003F18P6 (Cortex-M0+, 24 MHz)", prefix="arm-none-eabi-",
        bins=["C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/*/bin",
              "C:/Program Files/Arm GNU Toolchain arm-none-eabi/*/bin",
              os.path.join(LOCAL, "xpack", "xpack-arm-none-eabi-gcc-*", "bin")],
        arch=["-mcpu=cortex-m0plus", "-mthumb"], ld="py32f003.ld", flash=63 * 1024, ram=8 * 1024,
        isrs=("TIM1_BRK_UP_TRG_COM_IRQHandler", "SysTick_Handler"), hw_frame=32),   # M0+ stacks 8 words
    "ch32v003": dict(
        title="CH32V003F4P6 (RV32EC, 48 MHz)", prefix="riscv-none-elf-",
        bins=[os.path.join(LOCAL, "xpack", "xpack-riscv-none-elf-gcc-*", "bin")],
        arch=["-march=rv32ec_zicsr", "-mabi=ilp32e"], ld="ch32v003.ld", flash=16 * 1024 - 64, ram=2 * 1024,
        isrs=("TIM2_IRQHandler", "SysTick_Handler"), hw_frame=0),                    # handlers save their own regs
}


def find_bin(t):
    exe = shutil.which(t["prefix"] + "gcc")
    if exe:
        return os.path.dirname(exe)
    for pattern in t["bins"]:
        hits = sorted(glob.glob(pattern))
        if hits:
            return hits[-1]
    return ""


def run(cmd, **kw):
    r = subprocess.run(cmd, capture_output=True, text=True, cwd=HERE, **kw)
    if r.returncode:
        print(r.stdout, r.stderr)
        sys.exit(f"FAILED: {os.path.basename(cmd[0])}")
    return r.stdout + r.stderr


def sections(size, path):
    return {m.group(1): int(m.group(2)) for m in re.finditer(r"^(\.\S+)\s+(\d+)", run([size, "-A", "-d", path]), re.M)}


def module_size(size, obj):
    flash = ram = 0
    for name, n in sections(size, obj).items():
        if name.startswith((".bss", ".sbss")):
            ram += n
        elif not name.startswith((".comment", ".note", ".ARM.attributes", ".riscv.attributes", ".stack_sizes",
                                  ".debug", ".rel", ".group")):
            flash += n
    return flash, ram


def elf_size(size, elf):
    s = sections(size, elf)
    data = s.get(".data", 0)
    return sum(s.get(k, 0) for k in FLASH_SECTIONS) + data, data + s.get(".bss", 0)


def stack_depth(cis, isrs, hw_frame):
    """Worst call chain from main plus every interrupt (as if all nested), from GCC's -fcallgraph-info=su."""
    frame, edges = {}, {}
    for ci in cis:
        txt = open(ci).read()
        for m in re.finditer(r'node: \{ title: "([^"]+)" label: "([^"]*)"', txt):
            b = re.search(r"(\d+) bytes \((\w+)", m.group(2))
            frame[m.group(1)] = int(b.group(1)) if b else 0
        for m in re.finditer(r'edge: \{ sourcename: "([^"]+)" targetname: "([^"]+)"', txt):
            edges.setdefault(m.group(1), set()).add(m.group(2))
    memo = {}

    def depth(f, seen=()):
        if f in seen:
            return 0
        if f not in memo:
            memo[f] = frame.get(f, 0) + max((depth(g, seen + (f,)) for g in edges.get(f, ())), default=0)
        return memo[f]
    worst = max(frame.items(), key=lambda kv: kv[1])
    return depth("main"), sum(depth(i) + hw_frame for i in isrs if i in frame), worst


def build(key):
    t = TARGETS[key]
    bin_dir = find_bin(t)
    if not bin_dir:
        print(f"\n=== {t['title']}: {t['prefix']}gcc not found, skipped")
        return None
    gcc, size, nm = (os.path.join(bin_dir, t["prefix"] + tool) for tool in ("gcc", "size", "nm"))
    out = os.path.join(HERE, "build", key)
    os.makedirs(out, exist_ok=True)
    drv = os.path.join(HERE, key)
    flags = [*t["arch"], *CFLAGS, "-I", HERE, "-I", drv]
    real_ld = os.path.join(drv, t["ld"])
    # measuring build: same layout with room to spare, so an overflow still produces numbers to report
    ld = open(real_ld).read()
    ld = re.sub(r"(FLASH \(rx\)\s*:\s*ORIGIN = \w+, LENGTH = )[^\n]+", r"\g<1>256K", ld)
    ld = re.sub(r"(RAM\s+\(rwx\)\s*:\s*ORIGIN = \w+, LENGTH = )[^\n]+", r"\g<1>64K", ld)
    ld = re.sub(r"\n\s*BOND [^\n]+", "", ld)
    measure_ld = os.path.join(out, "measure.ld")
    open(measure_ld, "w").write(ld)
    link = ["-nostartfiles", "-nostdlib", "-Wl,--gc-sections"]
    srcs = COMMON + [os.path.join(key, d) for d in DRIVERS]

    print(f"\n=== {t['title']}")
    print(run([gcc, "--version"]).splitlines()[0])
    # 1) per-module sizes (no LTO) + stack / call-graph info
    objs, rows = [], []
    for c in srcs + ["libc.c"]:
        o = os.path.join(out, os.path.basename(c).replace(".c", ".o"))
        run([gcc, *flags, "-fstack-usage", "-fcallgraph-info=su", "-c", c, "-o", o])
        objs.append(o)
        rows.append((c.replace("\\", "/"), *module_size(size, o)))
    elf0 = os.path.join(out, "pixelpine_nolto.elf")
    run([gcc, *flags, *objs, *link, "-T", measure_ld, "-lgcc", "-o", elf0])
    # 2) the real thing: whole-program LTO (memset/memcpy stay outside LTO)
    elf = os.path.join(out, "pixelpine.elf")
    lto = [*srcs, os.path.join(out, "libc.o")]
    run([gcc, *flags, "-flto", *lto, *link, "-T", measure_ld, "-lgcc", "-Wl,-Map=" + elf[:-4] + ".map", "-o", elf])
    real = subprocess.run([gcc, *flags, "-flto", *lto, *link, "-T", real_ld, "-lgcc",
                           "-o", os.path.join(out, "pixelpine_real.elf")], capture_output=True, text=True, cwd=HERE)
    fits = "YES" if real.returncode == 0 else "NO - " + (
        re.findall(r"overflowed by \d+ bytes", real.stderr) or [real.stderr.strip()[-200:]])[0]

    syms = run([nm, elf])
    floats = sorted(set(re.findall(r"\b(__aeabi_[fd](?:add|sub|mul|div|cmp\w*|2\w+)|__aeabi_[iu]l?2[fd]|"
                                   r"__(?:add|sub|mul|div|fix|float|extend|trunc|cmp|lt|gt|le|ge|eq|ne)\w*[sd]f\w*)\b", syms)))
    helpers = sorted(set(re.findall(r"\b(__aeabi_u?idiv(?:mod)?|__aeabi_[lu]\w+|__(?:mul|div|udiv|mod|umod)[sd]i3)\b",
                                    syms)))

    print("Per-module (compiled separately, -Os, before LTO):")
    print(f"  {'module':20s} {'flash B':>8s} {'RAM B':>6s}")
    for c, fl, rm in rows:
        print(f"  {c:20s} {fl:8d} {rm:6d}")
    f0, r0 = elf_size(size, elf0)
    f1, r1 = elf_size(size, elf)
    cis = {os.path.basename(p): p for d in (out, HERE, drv) for p in glob.glob(os.path.join(d, "*.ci"))}
    main_d, isr_d, worst = stack_depth(cis.values(), t["isrs"], t["hw_frame"])
    stack = main_d + isr_d
    print(f"Linked without LTO : flash {f0:6d} B, static RAM {r0:5d} B")
    print(f"Linked with LTO    : flash {f1:6d} B of {t['flash']} ({100 * f1 / t['flash']:.0f} %), static RAM {r1} B")
    print(f"Stack (call graph) : main path {main_d} B + interrupts {isr_d} B = {stack} B"
          f"   (largest frame: {worst[0]} {worst[1]} B)")
    print(f"RAM total          : {r1 + stack} B of {t['ram']} ({100 * (r1 + stack) / t['ram']:.0f} %)")
    print(f"Fits the real linker script ({t['ld']}): {fits}")
    print(f"Soft-float symbols : {floats or 'none'}")
    print(f"libgcc int helpers : {helpers or 'none'}")
    for f in glob.glob(os.path.join(HERE, "*.ci")) + glob.glob(os.path.join(HERE, "*.su")) + \
            glob.glob(os.path.join(drv, "*.ci")) + glob.glob(os.path.join(drv, "*.su")):
        os.replace(f, os.path.join(out, os.path.basename(f)))
    return dict(title=t["title"], flash=f1, flash_max=t["flash"], ram=r1 + stack, ram_max=t["ram"], fits=fits)


def main():
    run([sys.executable, "gen_tables.py"])
    keys = sys.argv[1:] or list(TARGETS)
    res = {k: build(k) for k in keys}
    done = {k: v for k, v in res.items() if v}
    if len(done) > 1:
        print("\n=== Summary (LTO build)")
        for v in done.values():
            print(f"  {v['title']:38s} flash {v['flash']:6d} B / {v['flash_max']:5d} B "
                  f"({100 * v['flash'] / v['flash_max']:3.0f} %)   RAM {v['ram']:5d} B / {v['ram_max']} B "
                  f"({100 * v['ram'] / v['ram_max']:3.0f} %)   fits: {v['fits']}")


if __name__ == "__main__":
    main()
