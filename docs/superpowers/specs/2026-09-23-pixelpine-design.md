# PixelPine — design spec (approved 2026-09-23)

A ~70 mm tall PCB Christmas tree covered in a dense charlieplexed matrix of
240 warm-white LEDs with software PWM, showing soft idle animations.
Goal: lowest possible BOM + assembly cost ("compete with China"), price over reliability.

## Hardware

| Item | Decision |
|---|---|
| MCU | Puya PY32F003F18P6, TSSOP-20, LCSC C55338319. Cortex-M0+ at 24 MHz on the internal oscillator, runs straight from 5 V (no LDO). Was WCH CH32V003F4P6 until the firmware test compile, see the changelog. |
| LED matrix | 16 GPIO charlieplex → 16×15 = **240 LEDs**. Pins: PA1, PA2, PC0–PC7, PD0, PD2–PD6. |
| LEDs | 0603 warm white 2500–3000 K, XINGLIGHT XL-1608WWC-06, LCSC C965810 (JLC extended part, one feeder fee). |
| Current limit | 16 × 100 Ω 0402 (C25076, basic), one per charlieplex line. Worst case common-pin current ≈ 15–17 mA (< 20 mA pin rating). |
| Touch | PD7 (NRST reconfigured as GPIO via option bytes) → bare-copper star pad at the tree top, RC-timing capacitive sensing. Cycles animations. Zero BOM cost. |
| Programming | PD1/SWIO + VDD + GND on 3 test pads (pogo jig, WCH-LinkE). |
| Power | USB-C 6-pin power-only receptacle, SHOU HAN TYPE-C 6P, LCSC C456012, 2 × 5.1 kΩ CC pull-downs (C25905). 10 µF 0603 bulk (C19702) + 100 nF 0402 (C1525) decoupling. No fuse, no TVS, no switch. |
| PCB | 2-layer FR-4 1.6 mm, green mask, white silk, **lead-free HASL** (RoHS). Single-side SMT (top only). |
| Shape | 3-tier fir silhouette, 70 mm total height incl. trunk; copper star on top; MCU/USB/resistors on the trunk. USB-C exits on the trunk's side edge. |
| Stand | Separate PCB "foot" fin in the same panel; cross-lap slot joint with the trunk (no solder, no parts). |

## LED drive scheme (for simulator + later firmware)
- Scan by anode: phase *p* (0–15) drives line *p* HIGH, lines of lit LEDs in that phase LOW, rest Hi-Z. 16 phases/frame.
- LED id = (anode a, cathode c), a≠c. The high pin sources all LEDs lit in its phase → brightness sags as k (LEDs lit simultaneously) grows:
  I_total(k) ≈ (V − Vf) / (R + R/k + Rdrv(1+1/k)), per-LED = I_total/k. Firmware/simulator can compensate.
- PWM: binary-code modulation, 6 bit default (64 levels) with gamma; target ≥ 200 Hz refresh.

## Simulator (PC app)
Single self-contained `simulator/index.html` (no build, no server). Loads LED coordinates exported from the KiCad layout
(`hardware/led_map.json` embedded as JS). Canvas render with glow, per-LED 0..1 brightness. Hardware-realism toggle:
bit-depth quantisation, gamma, refresh rate, current-sag model. Controls: animation picker, speed, auto-playlist with crossfade,
touch-star click = next animation. Animations are pure functions `(state, t, dt) → brightness[240]` using integer-friendly math
so they port to C easily.

### Approved animations
Twinkle, Snowfall, Breathe, Spiral garland, Candle flicker, Star ripple, Fireflies, Rising sparks, Fill & drain,
Northern lights, Fireworks, Ornaments, **Soft radar** (wide, low-contrast glow sweep — softened per feedback),
Heartbeat, Tinsel shimmer, Scrolling text, Auto-playlist. (Game of Life dropped.)

## Deliverables
1. `hardware/` KiCad 10 project: schematic (ERC-checked), PCB with outline, stand, all footprints placed and routed (2 layers, Freerouting), renders.
2. `docs/BOM.csv` + `docs/bom.md` — LCSC/JLC part numbers, verified prices.
3. `simulator/index.html`.
4. `docs/market-analysis.md`.
5. `docs/eu-compliance/` — applicability analysis, test plan, DoC template, label/marking, technical file index.
6. `docs/cost-analysis.md` — 100 / 1k / 10k units incl. JLC assembly.
7. `docs/power-and-connectivity.md`.

## Out of scope
Production firmware, real EMC testing.

## Changes after approval (same day)

- **"Make it more cute"** (user request): puffy rounded star, chubby tiers with convex sides and rounded tips,
  rounded trunk and stand corners, silkscreen snowflakes; simulator restyled (Fredoka/Nunito, pill controls,
  background snow, wiggling star). LED grid refitted: 2.12 mm pitch.
- LED grid changed from hex to **square grid with LEDs at 45°**: at hex pitch the 0603 pads of 60° neighbours collide.
- **R19 1 kΩ** added between the star pad and PD7 for EN 61547 ESD immunity (~$0.004).
- USB-C C456012 uses `PixelPine:USB_C_SHOUHAN_TYPE-C-6P`, generated from LCSC's EasyEDA footprint (6 SMD pads + 4
  plated shell slots, paste-in-hole). The 3D view borrows KiCad's GCT USB4125 model, which has the same body size.
- **Pine, the character** (user direction): the tree's job is showing light shows but he's bad at it, drifting into
  face-only daydreams, sleeping, getting grumpy or excited. Emotions: energy, focus, mood, annoyance, excitement, bond
  (bond persisted in flash). Faces: 16 behaviours; transitions: star iris, sleepy blink, sparkle dissolve, snow
  blanket, jelly wobble, crossfade, plus face morphing. Heart eyes and Scrolling text removed (15 light shows remain).
- **Richer touch**: touch moved from PD7 (RC timing) to **PD4 / ADC A7** (charge sharing, sub-pF resolution); PD7
  becomes charlieplex line L13. The star footprint became a two-sided pad joined by a 0.6 mm plated hole, so a
  front+back pinch reads as "squeeze". Gestures: hover, tap, pet, press, squeeze, tickle. Ambient light from the LEDs.
- **Schematic**: blocks are now wired (MCU → line resistors, decoupling on VDD, USB-C → CC resistors / bulk cap,
  LED rows on an anode bus); net labels only cross between blocks.
- **MCU swapped to Puya PY32F003F18P6** (2026-09-24). A test compile of the full firmware measured 20.3 KB flash
  and 1.8 KB RAM, which overflows the CH32V003 (16 KB). The PY32F003F18P6 (LCSC C55338319) has 64 KB flash and
  8 KB RAM, a Cortex-M0+ with a hardware multiplier (24 MHz: it has no PLL, so 32 MHz would need a crystal),
  and costs $0.128 at 4 k (vs $0.139). TSSOP-20 pinout 1: lines on PA0–PA6, PA12, PA13/SWDIO, PA14/SWCLK, PB5–PB7, PF0, PF1, PF4/BOOT0; touch on PA7 (ADC);
  NRST kept for connect-under-reset SWD programming (5 pads). Project symbol generated from the datasheet.
- **PY32F003 test compile** (2026-09-24): the same firmware built with the Arm GNU Toolchain 14.2 for the
  PY32F003 measures 16.7 KB flash (26 %) and 1.9 KB RAM (23 %). The size test now has drivers for both MCUs.
- **USB-C footprint** (2026-09-24): J1 now uses `PixelPine:USB_C_SHOUHAN_TYPE-C-6P`, generated from LCSC's EasyEDA
  footprint for C456012, replacing the GCT USB4125 placeholder.
- **Markings** (2026-09-24): manufacturer MachStuff s.r.o., Jehličková 240, 240 24 Stromečkov (made-up address) on the back
  silkscreen and in the EU documents; the stand
  fin now says "PixelPine" instead of "Merry Christmas" (Pine is the character, so nobody would be saying it).
