# Technical file — PixelPine rev A

Kept by the manufacturer for **10 years** after the last unit is placed on the market (EMC Art. 7, RoHS Art. 7,
GPSR Art. 9). Market-surveillance authorities (CZ: ČOI, and ČTÚ for EMC) can request it in Czech or English.

## Index

| # | Document | Source / status |
|---|---|---|
| 1 | General product description, intended use, photos | this file (§ Description) + `hardware/renders/` |
| 2 | Schematic | `hardware/pixelpine_schematic.pdf` ✅ |
| 3 | PCB layout, assembly drawings, BOM with manufacturer part numbers | `hardware/pixelpine.kicad_pcb`, `docs/bom.md`, `hardware/production/` ✅ |
| 4 | List of standards applied | EN IEC 55015:2019+A11:2020, EN 61547:2009, EN IEC 63000:2018; EN 60598-2-20 and EN 62471 used as state-of-the-art references for the GPSR risk assessment |
| 5 | EMC test report | accredited lab, ⏳ planned ([test-plan.md](test-plan.md)) |
| 6 | EMC assessment (why the product meets the essential requirements, with design measures) | [README.md § Design decisions](README.md#design-decisions-that-were-made-for-compliance) ✅ |
| 7 | RoHS: supplier declarations for all 8 LCSC parts + PCB (LF-HASL) + XRF screen | ⏳ collect from LCSC product pages / JLC |
| 8 | REACH SVHC statement | ⏳ supplier declarations |
| 9 | GPSR risk assessment | this file (§ Risk assessment) ✅ |
| 10 | Photometric note: ≈ 2 lm, so outside 2019/2020 and 2019/2015 | `docs/power-and-connectivity.md` ✅ |
| 11 | EU Declaration of Conformity (signed) | [declaration-of-conformity.md](declaration-of-conformity.md), ⏳ sign after 5 and 7 |
| 12 | Labelling, packaging and user information | [labelling-and-instructions.md](labelling-and-instructions.md) ✅ |
| 13 | Production control: programming + functional test procedure, batch numbering scheme | this file (§ Production control) ✅ |
| 14 | Firmware version identification | firmware version readable over SWD; version stored on the batch record |

## Description

A 70 mm tall, 1.6 mm FR-4 printed circuit board in the shape of a Christmas tree, with 240 warm-white 0603 LEDs,
a PY32F003 microcontroller and a USB-C power-only receptacle. It stands on a slotted FR-4 stand fin. It is powered
from any USB power source at 5 V and draws at most 19 mA (0.1 W). A capacitive touch electrode shaped as a star
(on both faces of the board) lets the user interact with the tree's animated character. Indoor decorative use only.
No battery, no radio, no mains connection.

## Risk assessment (GPSR Art. 6 & 9)

Method: hazard → scenario → severity (1–4) × probability (1–5) → risk. Anything ≥ 8 must be reduced.

| Hazard | Scenario | S | P | Risk | Measures | Residual |
|---|---|:-:|:-:|:-:|---|:-:|
| Electric shock | User touches the circuitry | 1 | 1 | 1 | SELV 5 V from a CE USB adapter; no mains inside | 1 |
| Overheating / fire | Short circuit from a metallic object on the board | 2 | 2 | 4 | Max 0.1 W normally. A USB source current-limits a short (USB 2.0: 500 mA+); FR-4 is UL94 V-0. Leaflet: "don't leave near flammable decorations while unattended" — conservative wording | 2 |
| Overheating | Faulty/cheap USB adapter delivering > 5.5 V | 2 | 2 | 4 | MCU abs. max 6.5 V; failure mode is a dead MCU, not fire (currents limited by 100 Ω line resistors). Leaflet: use a CE-marked adapter | 2 |
| Cuts / pokes | Sharp PCB tips | 2 | 2 | 4 | **Rounded outline** (min radius 0.6 mm at inner joins, 2.4 mm at tips), routed edges | 1 |
| Choking | Child swallows the stand fin or an LED knocked off | 3 | 2 | 6 | Not a toy, not marketed to children; warning on the box: "Not suitable for children under 3 years – small parts"; LEDs are reflow-soldered | 4 |
| Eye (photobiological) | Staring at the LEDs from close up | 1 | 3 | 3 | ≈ 2 lm total, 2700 K, 1.1–8.7 mA per LED: Exempt Group per EN 62471 by inspection | 1 |
| Photosensitive epilepsy | Flashing animation | 3 | 1 | 3 | No animation or face flashes above 3 Hz with large-area contrast (Fireworks bursts, sneeze sparkle and sparkle transitions are local or < 1 s; the fastest full-area change is a 0.9 s dissolve); design rule for all future content | 2 |
| Tip-over | Tree knocked over on a table | 1 | 3 | 3 | Stand fin 36 mm; low mass; no hazard beyond the fall | 1 |
| Chemical | Lead in solder / finish | 2 | 1 | 2 | RoHS: lead-free solder paste and LF-HASL | 1 |
| Misuse outdoors | Used in rain | 2 | 2 | 4 | "Indoor use only" on product + box; 5 V cannot cause a shock | 2 |

Conclusion: no residual risk ≥ 8. The product is safe under normal and reasonably foreseeable use, with the listed
warnings.

## Production control (Module A — internal production control)

1. **Incoming:** JLC/CM supplies the certificate of conformity for the PCB (LF-HASL, UL94 V-0) and the reel labels
   (lot numbers) for the LEDs and MCU.
2. **Programming:** pogo jig on TP1–TP3, TP5, TP6 (SWD, connect-under-reset), flash, read-back verify.
3. **Functional test (8 s, automatic after flashing):** the test firmware lights each of the 16 scan phases in turn.
   The operator sees a "rolling" pattern, so a dead line shows as a missing diagonal. Then all 240 LEDs light
   dimly for a visual check of every LED. Then a touch test on the star.
4. **Batch numbering:** `YYWW-L` (year, week, line) written on the back silkscreen field with a paint marker, or
   laser-printed on the box label. The batch record stores firmware version, LED reel lot and test yield.
5. **Change control:** any change of the LED part, MCU, PCB outline, clock speed or scan timing triggers an EMC
   pre-scan and a review of whether the test report still covers the product.
