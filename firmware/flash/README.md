# Flashing a PixelPine board

Tested 2026-11-03 on the first assembled rev A board with a SEGGER J-Link EDU (HW V11): connect, chip erase,
program, read-back verify and run all work. The product firmware is built and flashed with
`python firmware/pixelpine/build.py flash` ([`../pixelpine/`](../pixelpine/README.md)), which calls this script.

## Wiring

Power the board from its own USB-C. The J-Link only senses the voltage and never supplies it: target power stays
off and `flash.py` turns it off explicitly.

| J-Link 20-pin | Board pad (back) | Net |
|---|---|---|
| 1 VTref | TP1 | +5 V (VBUS) |
| 4 GND | TP2 | GND |
| 7 SWDIO | TP3 | PA13 |
| 9 SWCLK | TP5 | PA14 |
| 15 RESET | TP6 | NRST |

## Flipper Zero instead of the J-Link

Flipper's **DAP Link** app (Apps > GPIO) makes it a CMSIS-DAP probe; `flash.py --cmsis` uses it. Tested 2026-11-03
on two blank boards. The Flipper's pins are 3.3 V and the PY32 at 5 V doesn't read them reliably, so **power the board
from the Flipper's 3V3 pin (pin 9) to TP1 during flashing, with the board's USB-C unplugged** (never both: USB's 5 V
would back-feed the Flipper). Wire GND, SWDIO and SWCLK as the DAP Link app's pinout screen shows; NRST isn't needed
for a blank board.

```bash
py -3 firmware/flash/flash.py --cmsis firmware/pixelpine/build/pixelpine.hex
```

Then remove the 3V3 wire and plug in USB-C to start Pine. A board that already runs Pine needs NRST (connect under
reset) to be reflashed.

## Setup (once per PC)

1. Install the SEGGER J-Link Software Pack (tested with V9.90): <https://www.segger.com/downloads/jlink/>
2. Install the Python tools: `py -3 -m pip install pyocd pylink-square`
3. Install Puya's device pack: `py -3 -m pyocd pack install PY32F003x8` (Puya.PY32F0xx_DFP 1.2.8)

J-Link V9.90 has no built-in PY32 flash loader, so the J-Link tools (J-Flash, Commander) can't program the chip.
pyOCD uses the flash algorithm from Puya's pack and drives the J-Link as a plain SWD probe.

## Use

```bash
py -3 firmware/flash/flash.py --info              # VTref, chip ID, boot mode, first flash words
py -3 firmware/flash/flash.py firmware.hex        # program + verify + reset (.hex, or .bin at 0x08000000)
py -3 firmware/flash/flash.py --erase firmware.hex
```

The script refuses to run if VTref is outside 4.6 to 5.3 V.

## What the first board showed

| Check | Result |
|---|---|
| VBUS at the MCU (VTref), blank chip | 4.97 V steady, no sag (from a USB-C charger) |
| SWD | DPIDR `0x0BC11477`, Cortex-M0+ r0p1, DBGMCU IDCODE `0x60001000` |
| Option bytes `FLASH_OPTR` | `0x0000BEAA`: RDP level 0, NRST pin kept as reset, nBOOT1 = 1 |
| Flash as delivered by JLC | blank (all `0xFF`) |
| Write speed | about 2.5 kB/s at 1 MHz SWD; the 64 KB image takes roughly 25 s *EST* |

**Boot after the first programming.** After flashing a blank chip, a *software* reset (SYSRESETREQ, which is the
default in pyOCD and J-Link) starts the ROM bootloader (`SYSCFG_CFGR1.MEM_MODE = 01`), not the new code. A reset
on the NRST pin boots main flash (`MEM_MODE = 00`, PC in our code). PF4/BOOT0 reads low, and the result is the
same with the SWD lines driven high or low, so the BOOT0 pin isn't the cause. The likely cause is a "flash empty"
flag that the chip latches at power-up and that only a pin or power-on reset clears, like the empty check on
STM32G0. `flash.py` therefore ends with an NRST reset.

Power-on boot checked: after flashing, unplugging and replugging USB-C, then attaching without any reset gives
`MEM_MODE = 00` and PC in the test loop. So a programmed board starts its own code from power-up.
