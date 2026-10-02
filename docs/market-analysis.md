# Market analysis — PixelPine

## One-line positioning

A **ready-built, cute 7 cm PCB Christmas tree with 240 warm-white LEDs and a personality**: Pine, who is supposed
to show you light shows but daydreams, naps, sulks and gets excited instead. Sold as an **affordable-premium gift
gadget (€10–15)**. It is not a soldering kit and not a garish RGB flasher. It's a tiny desk companion.

## Product category and price tier

| Dimension | Where PixelPine sits |
|---|---|
| Category | Seasonal decorative electronics / desk gadget / small gift (the "Secret Santa" shelf) |
| Price tier | **Affordable premium**: above €2–5 AliExpress kits, well below €25–35 boutique PCB art |
| Recommended shelf price | **€12.90** retail incl. VAT · **€14.90** own e-shop incl. VAT · **€6.90 ex-VAT** B2B promo with custom logo (500+ pcs) |
| Purchase type | Impulse / gift, decided in seconds from a photo or short video. The animation *is* the ad. |
| Seasonality | ~80 % of sales mid-November to 23 December; B2B orders placed September–October |

Why this tier works: the landed cost at volume is **€2.0–3.2** (see [cost-analysis.md](cost-analysis.md)).
That leaves room for a retailer's margin at €12.90 once volume passes ~3 000 pcs. In the own e-shop and
B2B channels it pays from the first 1 000 pcs.

## Target customers

| Segment | Who | Why they buy | Channel | Price sensitivity |
|---|---|---|---|---|
| **Gift buyers** (primary) | Office Secret Santa, colleagues, teachers, "something small for everyone" | Looks special, has a character people talk about ("mine was grumpy all morning"), needs no batteries | Gift shops, museum/design shops, e-shops, Christmas markets | Medium, with a €10–20 budget ceiling |
| **Tech enthusiasts & makers** | Engineers, students, electronics hobbyists | Dense visible SMD matrix, charlieplexing, open animation simulator, SWD pads to hack their own firmware | Tindie, Etsy, own e-shop, maker fairs, r/electronics & Hackaday style posts | Low. They pay for the story and the hackability. |
| **B2B promo** | Electronics distributors, engineering firms, universities (faculty gifts), IT companies | A useful, on-brand, non-plastic-junk giveaway. **Logo on the stand fin or back silkscreen costs €0 extra** | Direct sales, promo agencies | High, but orders are 500–5 000 pcs |
| Kids' rooms (secondary) | Parents | Night-light look | — | Not targeted: the product is *not a toy* (small parts, exposed electronics) and is labelled so |

## Competitive landscape

| Competitor type | Example | Price seen | What they offer | Our edge |
|---|---|---|---|---|
| DIY soldering kits (AliExpress) | "3D Christmas Tree soldering practice kit", 3/7-colour flashing LEDs ([example](https://www.aliexpress.com/item/4001159254399.html), [example](https://www.aliexpress.com/item/1005001519718393.html)) | **$2.53–4.33** incl. shipping | 12–36 THT LEDs, fixed flash pattern, you solder it yourself | Ready-built, 240 LEDs, real animations, warm white, EU seller with CE and GPSR accountability |
| Maker PCB ornaments (Tindie) | [Christmas Tree PCB](https://www.tindie.com/products/es/christmas-tree-pcb/) (ATtiny85 + WS2812) | **$3** bare PCB | Board only, build it yourself | Assembled and finished, far denser |
| Boutique PCB art (Tindie) | [Christmas Tree Pre-built](https://www.tindie.com/products/breadstick/christmas-tree-pre-built/) (coin cell, gold plating) | **$30** | Hand-made art piece, few LEDs | Same "PCB as art" appeal at under half the price, with far more motion |
| Mass-market LED decorations | Battery/USB acrylic or wooden LED trees in supermarkets and home-décor chains | typically €5–15 (indicative) | Static glow or simple flicker | Dense pixel animations, "tech" look, touch star |

**Takeaway:** we cannot beat AliExpress on price. A $2.53 kit shipped is below our volume landed cost, and many
such listings skip EU compliance costs that we must carry. We win on **looks, motion and being finished**, and we
compete in the gift slot, not the bargain bin.

## SWOT

| Strengths | Weaknesses |
|---|---|
| **Pine's personality**: a Tamagotchi-like bond that grows (and is remembered) keeps it on the desk after Christmas and gets clips shared · 240-LED density nobody else has at this price · calm warm-white animations · cute rounded design · USB-C, no batteries · touch star · €0 customisation (silkscreen) · tiny BOM besides LEDs | Price can't match AliExpress · strongly seasonal · needs a cable/charger (not included) · some power banks switch off at ~20 mA · 1 k units don't carry EMC costs in retail |
| **Opportunities** | **Threats** |
| Pine can outlive the season as a year-round desk companion · B2B logo editions (September–October orders smooth the season) · Easter/Valentine/Halloween shapes on the same electronics (new outline = new product) · "hack it" firmware community · a later USB-data variant driven live from the simulator | Copycats: the design is easy to clone once seen · LED price spikes · GPSR enforcement also levels the field, since non-compliant marketplace sellers are being delisted |

## Go-to-market plan (first season)

1. **September:** finish routing, order 100-pc pilot, program, shoot videos of the animations.
   The simulator already renders them, so marketing clips can be made before hardware exists.
2. **Early October:** EMC pre-scan and full test (see [eu-compliance/test-plan.md](eu-compliance/test-plan.md)).
   Open B2B pre-orders with logo mock-ups.
3. **Mid-October:** 1 000-pc JLC Standard PCBA run (≈ 2 weeks production + 1 week air freight).
4. **November–December:** own e-shop + Etsy/Tindie, 2–3 Czech design/gift shops on consignment, Christmas-market stall.

**Realistic first-season volume:** 600–1 500 units (≈ 300 retail/e-shop + 1–2 B2B orders of 500).
**Break-even on the €2 700 compliance budget** (contribution = revenue − €3.20 landed cost at 1 k):
~390 units through the own e-shop (€7.04 each), or ~730 units in B2B (€3.70 each).

## Pricing recommendation

- **€12.90** shelf price (retail), **€14.90** own e-shop (covers shipping subsidy), **€11.90** "two-pack" per unit.
- **B2B:** €6.90 ex-VAT at 500 pcs, €5.90 at 2 000 pcs, logo included. Stand-fin and back-silkscreen changes are
  free; a custom outline adds a one-off €150 engineering fee.
- Don't discount below €9.90 retail. The product's appeal is "special", and a low price signals "AliExpress".
