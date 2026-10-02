"""Rebuild every generated hardware artefact in order:
geometry -> footprints -> schematic (+ERC, netlist check) -> PCB -> autoroute -> DRC -> renders -> JLC files.
Run with any Python 3; the PCB steps are delegated to KiCad's bundled Python.
  python build_all.py              full build, routed with Freerouting (see route.py)
  python build_all.py --no-route   skip the autorouter (fast; DRC then only allows unrouted connections)
Since the 2026-10-01 hand edits in KiCad (GND pour etc.) it stops unless --regenerate is given, so the
hand-edited board isn't overwritten. Order files come from export_gerbers.py + export_fab.py instead.
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
HW = os.path.normpath(os.path.join(HERE, ".."))
KICAD = r"C:/Program Files/KiCad/10.0/bin"
CLI = os.path.join(KICAD, "kicad-cli.exe")
KPY = os.path.join(KICAD, "python.exe")


def step(title, cmd, cwd=HERE):
    print(f"== {title}")
    r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    out = "\n".join(l for l in (r.stdout + r.stderr).splitlines() if "memory leak" not in l)
    print(out.strip()[-600:])
    if r.returncode:
        sys.exit(f"step failed: {title}")


PCB_FILE = os.path.join(HW, "pixelpine.kicad_pcb")
if os.path.exists(PCB_FILE) and "(zone" in open(PCB_FILE, encoding="utf8").read() and "--regenerate" not in sys.argv:
    sys.exit("pixelpine.kicad_pcb has been edited by hand in KiCad (it has a copper zone, which the generator never "
             "makes). Rebuilding would discard those edits.\nFor order files use export_gerbers.py and export_fab.py; "
             "pass --regenerate to rebuild from the scripts anyway.")
step("geometry", [sys.executable, "geometry.py"])
step("footprints", [sys.executable, "gen_footprints.py"])
step("schematic", [sys.executable, "gen_sch.py"])
step("ERC", [CLI, "sch", "erc", "--severity-all", "--exit-code-violations", "-o", "erc.rpt", "pixelpine.kicad_sch"], HW)
step("netlist", [CLI, "sch", "export", "netlist", "--format", "kicadsexpr", "-o", "renders/_check.net",
                 "pixelpine.kicad_sch"], HW)
step("netlist check", [sys.executable, "check_netlist.py", os.path.join(HW, "renders", "_check.net")])
os.remove(os.path.join(HW, "renders", "_check.net"))
step("PCB", [KPY, "gen_pcb.py"])
ROUTE = "--no-route" not in sys.argv
if ROUTE:
    step("autoroute (Freerouting, takes a few minutes)", [KPY, "route.py"])
step("DRC" + ("" if ROUTE else " (unrouted board: only unconnected_items expected)"),
     [CLI, "pcb", "drc", "--schematic-parity", "--severity-all", "-o", "drc.rpt", "pixelpine.kicad_pcb"], HW)
with open(os.path.join(HW, "drc.rpt")) as f:
    kinds = {}
    for line in f:
        if line.startswith("["):
            k = line.split("]")[0][1:]
            kinds[k] = kinds.get(k, 0) + 1
print("   DRC summary:", kinds or "clean")
if set(kinds) - (set() if ROUTE else {"unconnected_items"}):
    sys.exit("unexpected DRC violations - see drc.rpt")
for side in ("top", "bottom"):
    step(f"render {side}", [CLI, "pcb", "render", "-o", f"renders/pcb_{side}.png", "--side", side, "-w", "1400",
                           "-h", "1600", "--quality", "high", "--background", "opaque", "pixelpine.kicad_pcb"], HW)
step("schematic PDF/SVG", [CLI, "sch", "export", "pdf", "-o", "pixelpine_schematic.pdf", "pixelpine.kicad_sch"], HW)
step("schematic SVG", [CLI, "sch", "export", "svg", "-o", "renders", "pixelpine.kicad_sch"], HW)
step("JLC BOM/CPL", [sys.executable, "export_fab.py"])
print("ALL OK")
