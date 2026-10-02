# Ordering PixelPine rev A at JLCPCB

Generated 2026-10-02 from `hardware/pixelpine.kicad_pcb` **as saved**, hand edits included (GND pour on both
layers, extra vias, rotated test pads). Two orders: the tree with assembly, and the stand fin as a bare PCB.

| File | Use |
|---|---|
| `pixelpine_tree_gerbers.zip` | **Order 1**: the tree, PCB + assembly (Gerbers + Excellon drill) |
| `jlc_bom.csv`, `jlc_cpl.csv` | **Order 1**, assembly step: bill of materials and placement file |
| `pixelpine_stand_gerbers.zip` | **Order 2**: the stand fin, PCB only (no parts, no holes) |
| `preview_tree_top/bottom.png`, `preview_stand_top/bottom.png` | 3D renders of exactly what went into the Gerbers |
| `gerbers_tree/`, `gerbers_stand/` | the same files unzipped, for a Gerber viewer |

Regenerate after editing the board in KiCad (it doesn't change the board):

```bash
"/c/Program Files/KiCad/10.0/bin/python.exe" hardware/scripts/export_gerbers.py
```

```bash
python hardware/scripts/export_fab.py
```

**Why two orders:** the fin is a separate piece in the KiCad file, and JLC counts every piece that can be separated
(milled, V-cut or tabbed) as an extra design, which costs extra
([JLC: what counts as different designs](https://jlcpcb.com/help/article/different-design-in-your-pcb-files)).
If you'd rather have one order, upload the tree zip with *Different design = 1* and order the fin separately later.
Both pieces must be **1.6 mm** thick: the cross-lap slots are 1.7 mm wide.

## Order 1: the tree (PCB + assembly)

Upload `pixelpine_tree_gerbers.zip` on jlcpcb.com → *Order now*. Settings (everything not listed: default):

| Setting | Value | Why |
|---|---|---|
| Base material / Layers | FR-4 / **2** | |
| Dimensions | 50.53 × 70.00 mm | read from the Gerber |
| Different design | 1 | |
| Delivery format | Single PCB | |
| PCB thickness | **1.6 mm** | the stand fin's slot is cut for 1.6 mm |
| PCB color / Silkscreen | Green / White | |
| Surface finish | **LeadFree HASL** | RoHS ([compliance plan](../../docs/eu-compliance/README.md)) |
| Outer copper weight | 1 oz | |
| Via covering | Tented | |
| Min via hole size / diameter | 0.3 mm / (0.4/0.45 mm) | the board uses 0.3 mm drills, 0.4 mm pads: no surcharge |
| Board outline tolerance | ±0.2 mm (regular) | |
| Mark on PCB | **Remove Mark** | otherwise JLC prints its order number wherever it likes; the board has no marker for "specify position" ([JLC ordering notes](https://jlcpcb.com/blog/common-pcb-design-issues-recommendations)) |
| Electrical test | Flying probe | |

Then switch on **PCB Assembly**:

| Setting | Value |
|---|---|
| PCBA type | Economic |
| Assembly side | Top side |
| Tooling holes | Added by JLCPCB (the board has none) |
| Confirm parts placement | **Yes**: check the preview below before production |

Upload `jlc_bom.csv` and `jlc_cpl.csv`: 8 part numbers, 263 placements per board (240 LEDs, 19 resistors,
2 capacitors, USB-C, MCU). The test pads and the touch star are bare copper and aren't in the files.

**In the placement preview, check:**

- **LEDs D1–D240:** each LED's cathode mark must sit on its **lower-left** pad (pad 1, seen from the front). LCSC's
  footprint for C965810 has the cathode on pad 1, the same as KiCad's, so the 45° rotations in the CPL need no
  correction. Look at a few LEDs in every row.
- **U1 (PY32F003, TSSOP-20):** the pin-1 dot belongs at the **top-left** corner, next to the ▶ printed on the board.
  LCSC's footprint for this part is drawn turned 90° compared with KiCad's, which usually means rotation 270 instead of 0.
  It looked right in the preview on 2026-09-24, so the CPL keeps 0. If it shows turned, fix it in the preview.
- **J1 (USB-C):** the mouth faces the trunk's right edge. Its footprint is LCSC's own, so it lines up.
- Resistors and capacitors aren't polarised.

Stock: one board takes 240 LEDs (XL-1608WWC-06, C965810). Check LCSC has 240 × boards plus JLC's attrition before
ordering. LED, MCU and USB-C are *extended* parts (a small fee per part type).

## Order 2: the stand fin (PCB only)

Upload `pixelpine_stand_gerbers.zip`. Same settings as the tree, **no assembly**:

| Setting | Value |
|---|---|
| Layers / Dimensions | 2 / 36.00 × 10.00 mm (no copper, silkscreen only) |
| PCB thickness | **1.6 mm** |
| Surface finish | LeadFree HASL (there's no exposed copper; keeps the RoHS paperwork simple) |
| Mark on PCB | Remove Mark |

## Board check (KiCad DRC, 2026-10-02)

- 0 unconnected items, 0 schematic-parity issues, zone fill up to date.
- 2 *starved thermal* errors: J1 pad B12 and R18 pad 2 reach the GND pour through one thermal spoke instead of
  the required two. Both pads also have their own GND track, so the connection is solid. This is cosmetic, and JLC
  doesn't check for it. To clear it, give those two pads a solid zone connection in KiCad, or set the zone's
  minimum spoke count to 1.
- Design rules match JLC's standard 2-layer process (no surcharges): 0.10 mm minimum track and space,
  0.3 mm drill / 0.4 mm vias, 0.3 mm copper-to-edge.

Don't run `hardware/scripts/build_all.py` on this board: it regenerates the PCB from the scripts and would discard
the hand edits. It now refuses to run unless you pass `--regenerate`.
