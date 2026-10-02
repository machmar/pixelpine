# EU compliance pack — PixelPine rev A

Product: USB-C (5 V, ≤ 0.1 W) decorative LED Christmas tree, 240 LEDs, no radio, no battery, not a toy.
Manufacturer placing it on the market: **MachStuff s.r.o.**, Jehličková 240, 240 24 Stromečkov, Czech Republic,
marekmach24@gmail.com (the address is made up for the school project). That makes us the "manufacturer" under EU law
even though assembly is subcontracted in China.

> This is an engineering-student compliance plan, not legal advice. Before the first sale, confirm the
> applicability matrix with the test lab or a notified-body consultant (a one-hour call, typically free with a
> test order).

## Applicability matrix

| Legislation | Applies? | Why | What we do |
|---|:---:|---|---|
| **EMC Directive 2014/30/EU** | **Yes → CE** | Electrical apparatus that can emit or suffer electromagnetic disturbance (fast GPIO edges, exposed touch pad) | Internal production control (Module A). Test to **EN IEC 55015** (emissions, lighting equipment) and **EN 61547** (immunity, lighting). [Test plan](test-plan.md) |
| **RoHS 2011/65/EU** + (EU) 2015/863 | **Yes → CE** | Electrical & electronic equipment | Lead-free HASL finish (**not** leaded HASL), RoHS declarations from LCSC for every part, 1 XRF screen of a finished unit. Technical file per **EN IEC 63000** |
| Low Voltage Directive 2014/35/EU | No | Only covers 50–1000 V AC / 75–1500 V DC; we run on 5 V DC | Electrical safety is handled under GPSR instead |
| **General Product Safety Regulation (EU) 2023/988 (GPSR)** | **Yes** | Consumer product; the LVD doesn't cover it | Risk assessment, technical documentation, product traceability markings, safety instructions in the local language, a contact for safety issues. [Technical file](technical-file.md) |
| **WEEE Directive 2012/19/EU** (CZ: Act 542/2020 Sb.) | **Yes** | EEE, category 5 (small equipment) | Crossed-out-bin symbol **on the product** (back silkscreen ✓). Register as a producer and join a take-back scheme (e.g. ASEKOL, ELEKTROWIN, REMA) in every country we sell to |
| **Packaging & Packaging Waste Regulation (EU) 2025/40 (PPWR)** | **Yes, from 12 Aug 2026** | We place packaging on the market | Minimal, recyclable kraft box, no plastic window; EPR registration (CZ: EKO-KOM). Harmonised sorting labels come later (2028+) |
| **REACH 1907/2006** | **Yes** | Articles | Art. 33 SVHC check via supplier declarations; answer consumer SVHC requests within 45 days |
| Toy Safety Directive 2009/48/EC / Regulation (EU) 2025/2509 | **No** | Annex I of both lists **Christmas decorations** as products *not* considered toys | Label "Not a toy. Decoration for indoor use." Design and marketing must not target children |
| Ecodesign for light sources (EU) 2019/2020 | No | The regulation covers light sources of **60–82 000 lm**; the whole tree emits ≈ **2 lm** ([calculation](../power-and-connectivity.md#consumption)) | Keep the calculation in the technical file |
| Energy labelling (EU) 2019/2015 | No | Same 60 lm scope as above | — |
| Radio Equipment Directive 2014/53/EU | No | No intentional radio | If a BLE variant ever ships: RED, EN 300 328, EN 18031 cybersecurity |
| Common charger (EU) 2022/2380 | No | Amends RED, radio equipment only. We still use USB-C, in the spirit of it | — |
| Batteries Regulation (EU) 2023/1542 | No | No battery | — |
| Product Liability Directive (EU) 2024/2853 | Yes (liability) | Covers any product | Product-liability insurance (~€150–300/yr for a small seller); keep the technical file for 10 years |
| Photobiological safety (EN 62471) | Hazard check only | Under GPSR, LED products are checked for blue-light / retinal hazard | ≈ 2 lm of 2700 K light is trivially **Exempt Group**; document the reasoning, no test needed |

**Result:** CE marking covers **EMC + RoHS** only. GPSR, WEEE, PPWR and REACH are "must do" obligations without
their own mark (WEEE has the bin symbol).

## Design decisions that were made *for* compliance

| Decision | Regulation it serves | Cost |
|---|---|---|
| Lead-free HASL surface finish | RoHS | ≈ +$0.02/board |
| **R19 1 kΩ** in series with the exposed star touch pad | EN 61547 ESD immunity (±4 kV contact / ±8 kV air) | $0.004 |
| 100 Ω on every charlieplex line + GPIO speed set to 2 MHz (firmware) | EN IEC 55015 radiated emissions: slow edges | €0, the resistors are needed anyway |
| No radio, no USB data | Avoids RED + cybersecurity testing | saves €3–5 k |
| Rounded board outline, no sharp tips | GPSR mechanical hazards (cuts, pokes) | €0, and it's cuter |
| Traceability marking on the back silkscreen (model, batch, manufacturer, address, e-mail) | GPSR Art. 9 | €0 |
| CE + WEEE logos on the back silkscreen | EMC/RoHS marking, WEEE Art. 14 | €0 |

## Roadmap and budget

| # | Step | When | Cost (EUR) |
|---|---|---|---:|
| 1 | Freeze rev A, route the board | T−8 weeks | — |
| 2 | Order 100-pc pilot; collect LCSC RoHS/REACH declarations for all 8 part numbers | T−7 w | in pilot cost |
| 3 | Write risk assessment + technical file draft ([technical-file.md](technical-file.md)) | T−6 w | own time |
| 4 | **EMC pre-scan** at a university lab (near-field probe + spectrum analyser, ESD gun) | T−5 w | 0–300 |
| 5 | **Accredited EMC test** EN IEC 55015 + EN 61547 (e.g. EZÚ Praha, ITC Zlín, or a DE/AT lab) | T−4 w | 1 500–2 500 |
| 6 | XRF RoHS screen of one finished unit | T−4 w | ~400 |
| 7 | Sign the EU Declaration of Conformity ([template](declaration-of-conformity.md)) | T−3 w | — |
| 8 | WEEE producer registration + take-back scheme; EKO-KOM packaging contract | T−3 w | ~300/yr + fees per kg |
| 9 | Print box and leaflet ([texts](labelling-and-instructions.md)) | T−3 w | in packaging cost |
| 10 | Product-liability insurance | T−2 w | 150–300/yr |
| 11 | Start selling. For online shops: show the manufacturer's contact, product photo, warnings and identifiers on the listing (GPSR Art. 19) | T | — |

Budget used in [cost-analysis.md](../cost-analysis.md): **€2 700** (steps 5, 6, 8), plus insurance.

## Files in this folder

- [test-plan.md](test-plan.md): EMC test list, setup, limits and risk points
- [technical-file.md](technical-file.md): technical documentation index + GPSR risk assessment
- [declaration-of-conformity.md](declaration-of-conformity.md): EU DoC template, ready to fill in
- [labelling-and-instructions.md](labelling-and-instructions.md): on-product marking, box texts, leaflet (EN + CZ)
