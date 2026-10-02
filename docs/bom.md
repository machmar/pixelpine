# Bill of materials — PixelPine rev A

All parts are stocked at [LCSC](https://www.lcsc.com) and placeable by JLCPCB PCBA. Prices are LCSC list prices fetched **2026-09-23**, shown at the break a 1 000-board run reaches (incl. 3% assembler attrition on multi-placement parts). Machine-readable version for JLC: [`hardware/production/jlc_bom.csv`](../hardware/production/jlc_bom.csv) + [`jlc_cpl.csv`](../hardware/production/jlc_cpl.csv).

| Ref | Part | LCSC | Qty | JLC class | Unit @1k | Per board |
|---|---|---|---:|---|---:|---:|
| D1-D240 | LED 0603 warm white 2500-3000 K, XINGLIGHT XL-1608WWC-06 | [C965810](https://www.lcsc.com/product-detail/C965810.html) | 240 | extended | $0.0055 | $1.36 |
| U1 | MCU Puya PY32F003F18P6 (TR), Cortex-M0+ (24 MHz on its internal oscillator), 64 KB flash, 8 KB SRAM, TSSOP-20 | [C55338319](https://www.lcsc.com/product-detail/C55338319.html) | 1 | extended | $0.148 | $0.148 |
| J1 | USB-C receptacle 6-pin power-only, SHOU HAN TYPE-C 6P (SMD) | [C456012](https://www.lcsc.com/product-detail/C456012.html) | 1 | extended | $0.023 | $0.023 |
| C1 | 10 uF 10 V X5R 0603, Samsung CL10A106KP8NNNC | [C19702](https://www.lcsc.com/product-detail/C19702.html) | 1 | basic | $0.022 | $0.022 |
| R1-R16 | 100 R 1 % 0402 line resistors, UNI-ROYAL 0402WGF1000TCE | [C25076](https://www.lcsc.com/product-detail/C25076.html) | 16 | basic | $0.0008 | $0.013 |
| C2 | 100 nF 16 V X7R 0402, Samsung CL05B104KO5NNNC | [C1525](https://www.lcsc.com/product-detail/C1525.html) | 1 | basic | $0.0034 | $0.0034 |
| R17, R18 | 5.1 k 1 % 0402 USB-C CC pull-downs, UNI-ROYAL 0402WGF5101TCE | [C25905](https://www.lcsc.com/product-detail/C25905.html) | 2 | basic | $0.0018 | $0.0037 |
| R19 | 1 k 1 % 0402 ESD series R for the touch star (price assumed = C25076) | [C11702](https://www.lcsc.com/product-detail/C11702.html) | 1 | basic | $0.0010 | $0.0010 |
| | **Total components** | | **263** | | | **$1.57** |

Not purchased (bare copper, zero cost): TP1–TP3, TP5, TP6 programming pads (back), TP4 capacitive star (front).

## Notes and substitutions

- **The LEDs are 86% of the component cost.** 240 × 0603 is the product; every other line is noise. A direct reel quote from XINGLIGHT or a comparable Shenzhen LED maker is the single biggest lever (≈ −30 %).
- **Cheaper, colder option:** KT-0603W (C2290) is a JLC *basic* white LED at $0.0060 @ 48 k — saves one extended-part fee ($3.07/order) but it is 40 000 K+ blue-white, not the warm look of the reference photo.
- **USB-C footprint:** `PixelPine:USB_C_SHOUHAN_TYPE-C-6P`, copied from LCSC's own EasyEDA footprint for C456012 with the same origin, so the CPL needs no offset. 6 SMD pads + 4 plated shell slots that are paste-in-hole reflowed with the rest of the board (JLC places it as an SMT part, no hand soldering).
- **No LDO, fuse, TVS or switch** — deliberate cost-down; the MCU is rated 1.7–5.5 V and runs from VBUS.
- **R19 (1 k)** was added after the EMC review: it protects the touch input (PA7) from ESD on the exposed star (≈ $0.004).
- **The star is a free sensor**: one copper electrode on both faces (a plated hole joins them) gives hover, tap, pet, press and a front-and-back squeeze; the LEDs double as an ambient light sensor. No sensor parts.
