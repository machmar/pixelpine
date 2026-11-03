"""Build the PixelPine firmware.

  python build.py                 product firmware   -> build/pixelpine.hex (Arm GNU Toolchain)
  python build.py showcycle       bring-up build: every light show for 8 s in turn
  python build.py host            PC test build      -> build/pine_host.exe (MSYS2 mingw64 gcc), used by test/
  python build.py flash [target]  build, then program the board through the J-Link (../flash/flash.py)
"""
import glob
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = ["src/fx.c", "src/jsmath.c", "src/shows.c", "src/face.c", "src/scene.c", "src/touch.c", "src/brain.c", "src/pine.c",
       "src/tables.c"]
COMMON = ["-std=gnu11", "-Wall", "-Wextra", "-Wno-unused-parameter", "-ffp-contract=off", "-Isrc"]


def tool(name, patterns):
    exe = shutil.which(name)
    if exe:
        return exe
    for p in patterns:
        hits = sorted(glob.glob(p))
        if hits:
            return hits[-1]
    sys.exit(f"{name} not found")


def run(cmd):
    r = subprocess.run(cmd, cwd=HERE, capture_output=True, text=True)
    if r.stdout.strip() or r.stderr.strip():
        print((r.stdout + r.stderr).strip())
    if r.returncode:
        sys.exit("build failed")
    return r.stdout


def arm(showcycle=False):
    gcc = tool("arm-none-eabi-gcc", ["C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/*/bin/arm-none-eabi-gcc.exe"])
    bindir = os.path.dirname(gcc)
    os.makedirs(os.path.join(HERE, "build"), exist_ok=True)
    name = "showcycle" if showcycle else "pixelpine"
    srcs = (["src/fx.c", "src/jsmath.c", "src/shows.c", "src/tables.c"] if showcycle else SRC) + ["hw/main.c", "hw/scan.c"]
    srcs += [] if showcycle else sorted({p.replace("\\", "/") for p in glob.glob("hw/*.c", root_dir=HERE)} - {"hw/main.c", "hw/scan.c"})
    elf = f"build/{name}.elf"
    run([gcc, "-mcpu=cortex-m0plus", "-mthumb", "-Os", "-g", "-ffunction-sections", "-fdata-sections",
         "-ffreestanding", *COMMON, "-Ihw", *(["-DSHOWCYCLE"] if showcycle else []), *srcs,
         "-nostartfiles", "-Thw/link.ld", "--specs=nano.specs", "-Wl,--gc-sections",
         f"-Wl,-Map=build/{name}.map", "-lm", "-o", elf])
    run([os.path.join(bindir, "arm-none-eabi-objcopy"), "-O", "ihex", elf, f"build/{name}.hex"])
    out = run([os.path.join(bindir, "arm-none-eabi-size"), "-A", elf])
    sec = {l.split()[0]: int(l.split()[1]) for l in out.splitlines() if l.startswith(".")}
    flash = sum(sec.get(s, 0) for s in (".isr_vector", ".text", ".ARM.exidx", ".data"))
    ram = sec.get(".data", 0) + sec.get(".bss", 0)
    print(f"{name}: flash {flash} B of {63 * 1024} ({flash / 63 / 1024:.0%}), RAM static {ram} B of 8192 "
          f"(stack gets the remaining {8192 - ram} B)")
    return f"build/{name}.hex"


def host():
    gcc = tool("gcc", ["C:/msys64/mingw64/bin/gcc.exe"])
    env_path = os.path.dirname(gcc)
    os.environ["PATH"] = env_path + os.pathsep + os.environ["PATH"]
    os.makedirs(os.path.join(HERE, "build"), exist_ok=True)
    run([gcc, "-O2", *COMMON, "-DPINE_HOST", *SRC, "host/host_main.c", "-lm", "-o", "build/pine_host.exe"])
    print("host: build/pine_host.exe")


if __name__ == "__main__":
    args = sys.argv[1:]
    if args[:1] == ["host"]:
        host()
    elif args[:1] == ["flash"]:
        hexfile = arm(showcycle=args[1:2] == ["showcycle"])
        subprocess.run([sys.executable, os.path.join(HERE, "..", "flash", "flash.py"), os.path.join(HERE, hexfile)],
                       check=True)
    else:
        arm(showcycle=args[:1] == ["showcycle"])
