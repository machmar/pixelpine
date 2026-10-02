"""Export JLCPCB assembly files into hardware/production/:
  jlc_bom.csv  (Comment, Designator, Footprint, LCSC Part #)
  jlc_cpl.csv  (Designator, Mid X, Mid Y, Layer, Rotation)
Reads the schematic (BOM) and the board as saved (positions), so hand edits in KiCad are kept.
Gerbers and drill files come from export_gerbers.py; the order guide is production/README.md.
"""
import csv
import os
import subprocess
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
HW = os.path.normpath(os.path.join(HERE, ".."))
OUT = os.path.join(HW, "production")
KICAD_CLI = r"C:/Program Files/KiCad/10.0/bin/kicad-cli.exe"


def run(*args):
    subprocess.run([KICAD_CLI, *args], check=True, capture_output=True)


def main():
    os.makedirs(OUT, exist_ok=True)
    raw_bom = os.path.join(OUT, "_bom_raw.csv")
    run("sch", "export", "bom", "--fields", "Reference,Value,Footprint,LCSC,${DNP}",
        "--labels", "Reference,Value,Footprint,LCSC,DNP", "--exclude-dnp", "--group-by", "",
        "-o", raw_bom, os.path.join(HW, "pixelpine.kicad_sch"))
    groups = defaultdict(list)
    with open(raw_bom, newline="", encoding="utf8") as f:
        for row in csv.DictReader(f):
            if not row["LCSC"]:
                continue          # test pads / touch star: bare copper, nothing to buy
            key = (row["Value"], row["Footprint"].split(":")[-1], row["LCSC"])
            groups[key].append(row["Reference"])
    os.remove(raw_bom)

    def natural(r):
        return (r.rstrip("0123456789"), int(r[len(r.rstrip("0123456789")):] or 0))

    with open(os.path.join(OUT, "jlc_bom.csv"), "w", newline="", encoding="utf8") as f:
        w = csv.writer(f)
        w.writerow(["Comment", "Designator", "Footprint", "LCSC Part #", "Qty"])
        for (val, fp, lcsc), refs in sorted(groups.items(), key=lambda kv: -len(kv[1])):
            refs.sort(key=natural)
            w.writerow([val, ",".join(refs), fp, lcsc, len(refs)])

    raw_pos = os.path.join(OUT, "_pos_raw.csv")
    run("pcb", "export", "pos", "--format", "csv", "--units", "mm", "--side", "both",
        "--exclude-dnp", "-o", raw_pos, os.path.join(HW, "pixelpine.kicad_pcb"))
    with open(raw_pos, newline="", encoding="utf8") as f, \
            open(os.path.join(OUT, "jlc_cpl.csv"), "w", newline="", encoding="utf8") as g:
        w = csv.writer(g)
        w.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
        n = 0
        for row in csv.DictReader(f):
            if row["Ref"].startswith("TP"):
                continue
            w.writerow([row["Ref"], f'{float(row["PosX"]):.3f}mm', f'{float(row["PosY"]):.3f}mm',
                        "Top" if row["Side"] == "top" else "Bottom", f'{float(row["Rot"]):.1f}'])
            n += 1
    os.remove(raw_pos)
    total = sum(len(v) for v in groups.values())
    print(f"BOM lines={len(groups)} placements={total}  CPL rows={n}  -> {OUT}")


if __name__ == "__main__":
    main()
