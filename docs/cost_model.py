"""PixelPine cost model -> writes docs/cost-analysis.md and docs/bom.md.

Every price is either an LCSC list price fetched 2026-09-23 or a clearly marked estimate.
Edit the tables below and re-run:  python docs/cost_model.py
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
EUR_PER_USD = 0.90          # planning rate; change here
ATTRITION = 0.03            # extra parts the assembler consumes (LEDs, passives)

# ---------------------------------------------------------------- BOM (per board)
# ref, description, LCSC, qty/board, JLC class, [(qty_break, usd), ...]  (LCSC list, 2026-09-23)
BOM = [
    ("D1-D240", "LED 0603 warm white 2500-3000 K, XINGLIGHT XL-1608WWC-06", "C965810", 240, "extended",
     [(50, .0122), (500, .0096), (1500, .0082), (5000, .0073), (20000, .0059), (40000, .0055)]),
    ("U1", "MCU Puya PY32F003F18P6 (TR), Cortex-M0+ (24 MHz on its internal oscillator), 64 KB flash, 8 KB SRAM, TSSOP-20", "C55338319", 1, "extended",
     [(5, .255), (50, .2006), (150, .1774), (500, .1483), (2500, .1354), (4000, .1276)]),
    ("J1", "USB-C receptacle 6-pin power-only, SHOU HAN TYPE-C 6P (SMD)", "C456012", 1, "extended",
     [(10, .0373), (100, .0301), (300, .0264), (1000, .0228), (5000, .0217), (10000, .0210)]),
    ("C1", "10 uF 10 V X5R 0603, Samsung CL10A106KP8NNNC", "C19702", 1, "basic",
     [(20, .0322), (200, .0252), (600, .0216), (4000, .0186), (8000, .0175), (20000, .0168)]),
    ("R1-R16", "100 R 1 % 0402 line resistors, UNI-ROYAL 0402WGF1000TCE", "C25076", 16, "basic",
     [(100, .0013), (1000, .001), (3000, .0009), (10000, .0008), (50000, .0007)]),
    ("C2", "100 nF 16 V X7R 0402, Samsung CL05B104KO5NNNC", "C1525", 1, "basic",
     [(100, .0046), (1000, .0034), (3000, .0029), (10000, .0025), (50000, .0024), (100000, .0023)]),
    ("R17, R18", "5.1 k 1 % 0402 USB-C CC pull-downs, UNI-ROYAL 0402WGF5101TCE", "C25905", 2, "basic",
     [(100, .0023), (1000, .0018), (3000, .0014), (10000, .0013), (50000, .0012)]),
    ("R19", "1 k 1 % 0402 ESD series R for the touch star (price assumed = C25076)", "C11702", 1, "basic",
     [(100, .0013), (1000, .001), (3000, .0009), (10000, .0008), (50000, .0007)]),
]
SMD_JOINTS = 240 * 2 + 19 * 2 + 2 * 2 + 20 + 10      # LEDs, resistors, caps, MCU, USB (6 pins + 4 shell tabs)


def unit_price(tiers, qty):
    price = tiers[0][1]
    for brk, p in tiers:
        if qty >= brk:
            price = p
    return price


def bom_cost(boards, factory_led_discount=0.0):
    rows, total = [], 0.0
    for ref, desc, lcsc, q, cls, tiers in BOM:
        need = q * boards * (1 + (ATTRITION if q > 1 else 0))
        p = unit_price(tiers, need)
        if lcsc == "C965810":
            p *= (1 - factory_led_discount)
        line = p * q * (1 + (ATTRITION if q > 1 else 0))
        rows.append((ref, lcsc, q, p, line))
        total += line
    return total, rows


# ---------------------------------------------------------------- scenarios (USD unless noted)
SCENARIOS = [
    dict(
        name="Pilot · 100 pcs", boards=100, how="JLCPCB Economic PCBA, parts from JLC/LCSC stock",
        pcb=0.95,                                   # EST: ~95 USD for 100 panels 52x83 mm, LF-HASL, contour routing
        asm_fixed=8.18 + 1.53 + 3 * 3.07,           # setup + stencil + 3 extended part feeders (LED, MCU, USB)
        joint=0.0016,
        led_discount=0.0,
        program=0.0, labour=0.0,                    # done by the team with an SWD probe + pogo jig
        pack=0.03,                                  # zip bag
        freight=0.30,                               # DHL, ~30 USD per 100
        broker=0.0,
    ),
    dict(
        name="Batch · 1 000 pcs", boards=1000, how="JLCPCB Standard PCBA, panelised 2-up",
        pcb=0.40,                                   # EST
        asm_fixed=25.56 + 8.21 + 8 * 1.53 + 8.21,   # setup + stencil + feeder fee per part type + panel fee
        joint=0.0012,                               # JLC tier for > 100 k joints per order
        led_discount=0.0,
        program=0.09, labour=0.19,                  # EST: CZ student job, 15 s programming + 30 s stand/test/pack @ ~20 EUR/h
        pack=0.18,                                  # printed kraft box + card insert
        freight=0.24,                               # air, ~40 kg @ 6 USD/kg
        broker=35.0,                                # customs clearance per shipment
    ),
    dict(
        name="Volume · 10 000 pcs", boards=10000, how="Shenzhen contract manufacturer, LEDs bought direct",
        pcb=0.22,                                   # EST: volume fab ~ CNY 400/m2 incl. LF-HASL, 75 % panel yield
        asm_fixed=150.0,                            # EST: stencil + programming fixture NRE
        joint=0.0010,                               # EST: typical CM rate per SMT point
        led_discount=0.30,                          # EST: reel price direct from the LED maker vs LCSC list
        program=0.02, labour=0.04,                  # done in China on the line
        pack=0.08,
        freight=0.05,                               # sea/rail consolidated
        broker=60.0,
    ),
]
DUTY = 0.037            # EST: CN 9405 lighting fittings / sets, 3.7 %; confirm in EU TARIC before import
COMPLIANCE_EUR = {      # one-off / first year, EUR (see eu-compliance/)
    "EMC test at an accredited lab (EN IEC 55015 + EN 61547)": 2000,
    "RoHS evidence (supplier declarations + 1 XRF screen)": 400,
    "WEEE + packaging EPR registration, first-year fees (CZ)": 300,
}


def run(sc):
    b = sc["boards"]
    bom, rows = bom_cost(b, sc["led_discount"])
    asm = SMD_JOINTS * sc["joint"] + sc["asm_fixed"] / b
    exw = bom + sc["pcb"] + asm + sc["program"] + sc["labour"] + sc["pack"]
    landed = exw + sc["freight"] + (exw + sc["freight"]) * DUTY + sc["broker"] / b
    comp = sum(COMPLIANCE_EUR.values()) / EUR_PER_USD / b
    return dict(bom=bom, rows=rows, pcb=sc["pcb"], asm=asm, program=sc["program"], labour=sc["labour"],
                pack=sc["pack"], exw=exw, freight=sc["freight"], landed=landed, comp=comp, total=landed + comp)


def usd(x):
    if x < 0.01:
        return f"${x:.4f}"
    return f"${x:,.3f}" if x < 1 else f"${x:,.2f}"


def eur(x):
    return f"€{x * EUR_PER_USD:,.2f}"


def eur_signed(v):
    return f"−€{-v:,.2f}" if v < 0 else f"€{v:,.2f}"


def main():
    res = [run(s) for s in SCENARIOS]

    # ------------------------------------------------ bom.md
    b1k = bom_cost(1000)[1]
    lines = ["# Bill of materials — PixelPine rev A", "",
             "All parts are stocked at [LCSC](https://www.lcsc.com) and placeable by JLCPCB PCBA. "
             "Prices are LCSC list prices fetched **2026-09-23**, shown at the break a 1 000-board run reaches "
             f"(incl. {ATTRITION:.0%} assembler attrition on multi-placement parts). "
             "Machine-readable version for JLC: [`hardware/production/jlc_bom.csv`](../hardware/production/jlc_bom.csv) "
             "+ [`jlc_cpl.csv`](../hardware/production/jlc_cpl.csv).", "",
             "| Ref | Part | LCSC | Qty | JLC class | Unit @1k | Per board |",
             "|---|---|---|---:|---|---:|---:|"]
    tot = 0
    for (ref, desc, lcsc, q, cls, tiers), (_, _, _, p, line) in zip(BOM, b1k):
        lines.append(f"| {ref} | {desc} | [{lcsc}](https://www.lcsc.com/product-detail/{lcsc}.html) | {q} | {cls} | "
                     f"{usd(p)} | {usd(line)} |")
        tot += line
    led_share = b1k[0][4] / tot
    lines += [f"| | **Total components** | | **{sum(r[3] for r in BOM)}** | | | **{usd(tot)}** |", "",
              "Not purchased (bare copper, zero cost): TP1–TP3, TP5, TP6 programming pads (back), TP4 capacitive star (front).", "",
              "## Notes and substitutions", "",
              f"- **The LEDs are {led_share:.0%} of the component cost.** 240 × 0603 is the product; every other line is noise. "
              "A direct reel quote from XINGLIGHT or a comparable Shenzhen LED maker is the single biggest lever (≈ −30 %).",
              "- **Cheaper, colder option:** KT-0603W (C2290) is a JLC *basic* white LED at $0.0060 @ 48 k — saves one "
              "extended-part fee ($3.07/order) but it is 40 000 K+ blue-white, not the warm look of the reference photo.",
              "- **USB-C footprint:** `PixelPine:USB_C_SHOUHAN_TYPE-C-6P`, copied from LCSC's own EasyEDA footprint for "
              "C456012 with the same origin, so the CPL needs no offset. 6 SMD pads + 4 plated shell slots that are "
              "paste-in-hole reflowed with the rest of the board (JLC places it as an SMT part, no hand soldering).",
              "- **No LDO, fuse, TVS or switch** — deliberate cost-down; the MCU is rated 1.7–5.5 V and runs from VBUS.",
              "- **R19 (1 k)** was added after the EMC review: it protects the touch input (PA7) from ESD on the exposed star (≈ $0.004).",
              "- **The star is a free sensor**: one copper electrode on both faces (a plated hole joins them) gives hover, "
              "tap, pet, press and a front-and-back squeeze; the LEDs double as an ambient light sensor. No sensor parts.",
              ""]
    with open(os.path.join(HERE, "bom.md"), "w", encoding="utf8") as f:
        f.write("\n".join(lines))

    # ------------------------------------------------ cost-analysis.md
    L = ["# Cost analysis — PixelPine rev A", "",
         f"Planning rate **1 USD = {EUR_PER_USD} EUR**. Generated by [`cost_model.py`](cost_model.py) — change an input there "
         "and re-run instead of editing numbers here. Items marked *EST* are engineering estimates; everything else is a "
         "list price (LCSC, JLCPCB published PCBA fees, fetched 2026-09-23).", "",
         f"Assembly volume per board: **{SMD_JOINTS} SMD joints** (480 of them are LEDs), 0 THT joints, one side only.", "",
         "## Unit cost by volume", "",
         "| | " + " | ".join(s["name"] for s in SCENARIOS) + " |",
         "|---|" + "---:|" * len(SCENARIOS),
         "| How | " + " | ".join(s["how"] for s in SCENARIOS) + " |"]

    def row(label, key, fmt=usd):
        L.append(f"| {label} | " + " | ".join(fmt(r[key]) for r in res) + " |")

    row("Components (BOM)", "bom")
    row("Bare PCB incl. stand fin *(EST)*", "pcb")
    row("SMT assembly (joints + amortised setup)", "asm")
    row("Programming (SWD pogo jig)", "program")
    row("Stand fitting, test, packing labour", "labour")
    row("Packaging", "pack")
    L.append("| **Ex-works cost** | " + " | ".join(f"**{usd(r['exw'])}**" for r in res) + " |")
    row("Freight to CZ", "freight")
    L.append(f"| **Landed in CZ** (+{DUTY:.1%} duty, broker) | " +
             " | ".join(f"**{usd(r['landed'])} / {eur(r['landed'])}**" for r in res) + " |")
    row("Compliance amortised over the batch", "comp")
    L.append("| **Total per unit** | " + " | ".join(f"**{usd(r['total'])} / {eur(r['total'])}**" for r in res) + " |")
    L += ["", "Compliance budget (first year, one-off): " +
          ", ".join(f"{k} €{v:,}" for k, v in COMPLIANCE_EUR.items()) +
          f" → **€{sum(COMPLIANCE_EUR.values()):,}**. Details in [eu-compliance/](eu-compliance/README.md).", ""]

    # price ladder
    r1k, r10k = res[1], res[2]
    L += ["## From cost to shelf price", "",
          "Retail prices include 21 % Czech VAT. Retail channel assumes the usual keystone (retailer takes ~50 % of the "
          "net price); own e-shop assumes ~12 % payment/platform fees and a €0.60 shipping subsidy.", "",
          "| Channel | Shelf price | Net of VAT | Our revenue / unit | Margin @1k | Margin @10k |",
          "|---|---:|---:|---:|---:|---:|"]
    for ch, shelf, share, extra in (("Retail / gift shop", 12.90, 0.5, 0), ("Own e-shop, Etsy, Tindie", 14.90, 0.88, 0.60),
                                    ("B2B promo (logo on the foot), 500+ MOQ", None, None, None)):
        if shelf is None:
            L.append("| B2B promo (custom silkscreen logo), 500+ MOQ | quoted ex-VAT | €6.90 | €6.90 | "
                     f"{eur_signed(6.90 - r1k['total'] * EUR_PER_USD)} | {eur_signed(6.90 - r10k['total'] * EUR_PER_USD)} |")
            continue
        net = shelf / 1.21
        rev = net * share - extra
        L.append(f"| {ch} | €{shelf:.2f} | €{net:.2f} | €{rev:.2f} | {eur_signed(rev - r1k['total'] * EUR_PER_USD)} | "
                 f"{eur_signed(rev - r10k['total'] * EUR_PER_USD)} |")
    L += ["", "## What the numbers say", "",
          f"- **The LEDs drive everything.** At 1 000 pcs the LEDs are {res[1]['rows'][0][4] / res[1]['bom']:.0%} of the "
          f"BOM, and their 480 joints are {480 / SMD_JOINTS:.0%} of the assembly bill. Halving the LED count would cut the "
          "unit cost by roughly a third — but 240 dense LEDs *are* the product; keep them.",
          "- **The MCU is almost free.** The PY32F003F18P6 (64 KB flash) is ~$0.13 in reels, cheaper than the 16 KB "
          "CH32V003 it replaced; a charlieplex driver chip such as the IS31FL3731 on the reference board costs over 10× more.",
          "- **At 100 pcs compliance kills the margin** — treat it as a pilot (samples, school fair, beta testers) and "
          "keep those units off the market until the EMC report exists. At 1 000 pcs the classic retail channel loses "
          "money because the €2 700 compliance budget is spread over too few units; own e-shop and B2B promo orders "
          "carry it. From ~3 000 pcs retail works, and at 10 000 pcs every channel is healthy.",
          "- **Competing with AliExpress** (bare soldering kits at $2.5–4.3 shipped) on price alone is not possible "
          "at the retail shelf — the landed volume cost is close to their selling price, and EU compliance plus VAT "
          "is a cost those listings often skip. The product has to win on looks (dense warm-white matrix, "
          "animations, ready-built) and sit in the €10–15 gift slot. See [market-analysis.md](market-analysis.md).", "",
          "## Cost-down levers not yet taken", "",
          "| Lever | Saving / unit | Trade-off |", "|---|---:|---|",
          "| Direct LED reel quote instead of LCSC list | ≈ $0.40 | needs 250 k+ LEDs per order |",
          "| Cold-white basic LED (C2290) | ≈ $0.05 + $3/order | loses the warm look |",
          "| Drop C1 (10 µF) and rely on C2 + USB source capacitance | ≈ $0.02 | more supply ripple on touch sensing |",
          "| Stand fin from the PCB panel's waste corners instead of its own area | ≈ $0.03 | tighter panel layout |",
          "| Kraft paper sleeve instead of a printed box | ≈ $0.10 | less gift appeal |", ""]
    with open(os.path.join(HERE, "cost-analysis.md"), "w", encoding="utf8") as f:
        f.write("\n".join(L))
    for s, r in zip(SCENARIOS, res):
        print(f"{s['name']:24s} BOM {r['bom']:.3f}  asm {r['asm']:.3f}  exw {r['exw']:.2f}  landed {r['landed']:.2f}  "
              f"total {r['total']:.2f} USD")


if __name__ == "__main__":
    main()
