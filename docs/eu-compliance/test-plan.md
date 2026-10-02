# EMC test plan — PixelPine rev A

Equipment under test (EUT): PixelPine rev A, standing on its stand fin, powered through a 1 m USB-A-to-C cable from a
CE-marked 5 V USB adapter (reference AE, listed in the report), running the **worst-case EMC firmware mode**:
all 240 LEDs cycling at 100 % duty, 8-bit BCM, fastest scan. This maximises edge rate and current steps.
A second run uses the default auto playlist.

Standards for general lighting equipment are used because the product is a decorative luminaire. Our lab
must agree this at quotation; the alternative multimedia-equipment pair EN 55032/55035 is **not** appropriate.

## Emissions — EN IEC 55015:2019 + A11:2020

| Test | Port | Range | Limit (class) | Relevance / risk |
|---|---|---|---|---|
| Radiated disturbance | Enclosure | 30–300 MHz (CDNE or OATS/SAC at 10 m / 3 m) | Table 10/11 limits | **Main risk.** 16 GPIO lines toggle with µs-scale BCM slots; harmonics of the 2 µs slot clock (500 kHz) and GPIO edges reach the VHF band. Mitigation: GPIO speed 2 MHz, 100 Ω series R, short loops. Spread-spectrum dithering of the slot timer is a free firmware fallback. |
| Conducted disturbance | DC/USB power port (via the cable to the adapter) | 9 kHz–30 MHz | Load/other-port limits per the lab's port classification | Low: ~10 mA average with 16 mA steps, and C1 10 µF buffers it |
| Conducted disturbance | AC mains | 9 kHz–30 MHz | — | Measured on the **adapter**, which carries its own CE; state in the report that the EUT has no mains port |
| Harmonics / flicker (EN IEC 61000-3-2/-3) | AC mains | — | N/A | < 75 W and no mains port |

## Immunity — EN 61547:2009 (+ IEC 61000-4-x basic standards)

| Test | Port | Level | Performance criterion | Relevance / risk |
|---|---|---|---|---|
| **ESD** IEC 61000-4-2 | Enclosure: **touch star, front and back** (contact), board edges and USB shell (air) | ±4 kV contact, ±8 kV air | **B** (temporary effect allowed, self-recovery) | **Main risk:** exposed copper (both faces) wired to ADC pin PA7. Mitigated by **R19 1 kΩ** + the MCU clamp diodes. Firmware: watchdog on, touch baseline re-learn after ESD glitches; a glitch may register as a gesture (Pine reacts), which is criterion B. |
| Radiated RF immunity IEC 61000-4-3 | Enclosure | 3 V/m, 80–1000 MHz, 80 % AM 1 kHz | A | Low: no analogue front-end; touch sensing may false-trigger, so debounce and require 3 consecutive samples |
| EFT/burst IEC 61000-4-4 | DC power port | ±0.5 kV (if the cable is > 3 m per the standard; ours is 1 m, so likely N/A) | B | Lab decides by port length |
| Conducted RF IEC 61000-4-6 | DC power port | 3 V, 0.15–80 MHz | A | Low |
| Surge IEC 61000-4-5 | DC power port | N/A for USB-powered with cable < 30 m | — | — |
| Voltage dips | AC | N/A (no mains port) | — | — |
| Magnetic field IEC 61000-4-8 | Enclosure | Only if magnetically sensitive parts, which we don't have | — | — |

**Performance criteria in plain language**
- **A:** the animation keeps running without visible change.
- **B:** a flicker or a jump to another animation is allowed, but it must recover by itself with no settings lost.
- **C:** needs re-plugging. Not allowed for ESD in this plan.

## Pre-compliance (before paying the lab)

1. **Near-field scan** of the trunk area and the charlieplex fan-out with an H-field probe and a spectrum
   analyser. Compare the full-on EMC firmware with a 25 % duty version.
2. **ESD gun** on the star: 10 discharges each at ±2, ±4 and ±6 kV contact. Pass = no reset or at most one glitch,
   and no latch-up (supply current returns to normal).
3. **Touch robustness:** 1 h next to a phone charger and a running laptop with no false touches.
4. Record everything in the technical file (photos of the setup, plots).

## Samples and paperwork for the lab

- 3 finished units (one kept as a reference sample) + the USB adapter and cable used
- The EMC firmware build (hex) and a description of the operating modes
- Schematic PDF, BOM, photos, a draft of the user leaflet
- Quote request text: *"EMC testing of a 5 V USB-powered decorative LED luminaire (no mains port, no radio) to
  EN IEC 55015:2019+A11:2020 and EN 61547:2009, with test report suitable for CE Module A self-declaration."*
