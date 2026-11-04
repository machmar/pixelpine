"""Flash a PixelPine board through a SEGGER J-Link (or any pyOCD probe) and verify it.

    python firmware/flash/flash.py image.hex        # or .bin (loaded at 0x08000000)
    python firmware/flash/flash.py --erase          # chip erase only
    python firmware/flash/flash.py --info           # VTref, chip ID, boot mode, first flash words

Needs: pip install pyocd pylink-square, then once: pyocd pack install PY32F003x8.
Connects under reset (NRST held low), because the product firmware turns SWDIO/SWCLK into LED lines.
Ends with a pin reset on NRST, not a software reset: after the first programming of a blank chip a software
reset still boots the ROM bootloader (the chip latched "flash empty" at power-up), a pin reset does not.
"""
import argparse, sys
from pathlib import Path

TARGET = "py32f003x8"
FLASH = 0x08000000
VT_MIN, VT_MAX = 4.6, 5.3                  # USB 5 V straight on VDD, no regulator


def check_vtref():
    try:
        import pylink
    except ImportError:
        return print("VTref: pylink not installed, skipped")
    jl = pylink.JLink()
    jl.open()
    jl.power_off()                          # the board is powered from its own USB-C, never from the probe
    v = jl.hardware_status.voltage / 1000
    jl.close()
    print(f"VTref: {v:.3f} V")
    if not VT_MIN <= v <= VT_MAX:
        sys.exit(f"VTref outside {VT_MIN}-{VT_MAX} V: is USB-C plugged in and J-Link pin 1 on TP1?")


def load_image(path):
    """Return [(address, bytes)] segments."""
    if path.suffix.lower() == ".bin":
        return [(FLASH, path.read_bytes())]
    if path.suffix.lower() == ".hex":
        from intelhex import IntelHex           # installed with pyocd
        ih = IntelHex(str(path))
        return [(a, ih.tobinstr(a, b - 1)) for a, b in ih.segments()]
    sys.exit("give a .hex or .bin (arm-none-eabi-objcopy -O ihex firmware.elf firmware.hex)")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("image", nargs="?", type=Path)
    ap.add_argument("--erase", action="store_true", help="chip erase (with an image: erase everything first)")
    ap.add_argument("--info", action="store_true")
    ap.add_argument("--speed", type=int, default=1_000_000, help="SWD clock in Hz")
    ap.add_argument("--cmsis", action="store_true",
                    help="use a CMSIS-DAP probe (e.g. Flipper Zero's DAP Link app) instead of the J-Link")
    ap.add_argument("--hold", type=float, default=0, metavar="S",
                    help="keep NRST low for S seconds after programming: unplug SWDIO/SWCLK meanwhile, so the "
                         "firmware never runs with the probe on its LED lines L8/L9")
    ap.add_argument("--no-nrst", action="store_true",
                    help="NRST not wired (blank board only): plain connect, no reset at the end; power-cycle to start")
    a = ap.parse_args()
    if not (a.image or a.erase or a.info):
        ap.error("nothing to do")

    if not a.cmsis:
        check_vtref()
    from pyocd.core.helpers import ConnectHelper
    from pyocd.flash.eraser import FlashEraser
    from pyocd.flash.file_programmer import FileProgrammer

    opts = {"connect_mode": "under-reset", "reset_type": "hw", "frequency": a.speed, "resume_on_disconnect": False}
    if a.no_nrst:
        opts.update(connect_mode="halt", reset_type="sw")
    uid = None
    if a.cmsis:                                   # the first probe that isn't a J-Link
        probes = [p for p in ConnectHelper.get_all_connected_probes(blocking=False) if "J-Link" not in p.description]
        if not probes:
            sys.exit("no CMSIS-DAP probe found (Flipper: Apps > GPIO > DAP Link, USB cable to this PC)")
        uid = probes[0].unique_id
        print(f"probe: {probes[0].description}")
    with ConnectHelper.session_with_chosen_probe(unique_id=uid, target_override=TARGET, options=opts) as s:
        t = s.target
        if a.erase:
            FlashEraser(s, FlashEraser.Mode.CHIP).erase()
            print("erased")
        if a.image:
            FileProgrammer(s).program(str(a.image), base_address=FLASH if a.image.suffix == ".bin" else None)
            t.halt()
            for addr, data in load_image(a.image):
                back = bytes(t.read_memory_block8(addr, len(data)))
                if back != data:
                    bad = next(i for i in range(len(data)) if back[i] != data[i])
                    sys.exit(f"VERIFY FAILED at 0x{addr + bad:08X}")
                print(f"verified {len(data)} bytes at 0x{addr:08X}")
        if a.info:
            t.halt()
            print(f"DBGMCU IDCODE 0x{t.read32(0x40015800):08X}")
            print(f"boot mode     {('main flash', 'system ROM', '?', 'SRAM')[t.read32(0x40010000) & 3]}")
            print(f"FLASH_OPTR    0x{t.read32(0x40022020):08X}")
            print("flash[0:16]  ", " ".join(f"{w:08X}" for w in t.read_memory_block32(FLASH, 4)))
        t.halt()
    if a.no_nrst or a.cmsis:
        print("done, core halted: unplug and replug the board's USB-C to start it")
        return
    # The firmware turns SWDIO/SWCLK into LED lines, so the debug session has to be closed before it runs:
    # pulse NRST from the probe instead of resetting through pyOCD.
    import pylink, time
    jl = pylink.JLink()
    jl.open()
    jl.set_reset_pin_low()
    if a.hold > 0:
        print(f"NRST held low for {a.hold:.0f} s: unplug SWDIO and SWCLK now", flush=True)
        t_end = time.time() + a.hold
        while time.time() < t_end:
            time.sleep(1)
            print(f"  {max(0, t_end - time.time()):.0f} s", flush=True)
    time.sleep(0.02)
    jl.set_reset_pin_high()
    jl.close()
    print("reset (NRST), running")


if __name__ == "__main__":
    main()
