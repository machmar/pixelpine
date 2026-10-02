"""Export JLCPCB fabrication files into hardware/production/ from pixelpine.kicad_pcb AS SAVED (hand edits kept):
  pixelpine_tree_gerbers.zip   the tree: Gerbers + Excellon drill, for the PCB + assembly order (jlc_bom/cpl.csv)
  pixelpine_stand_gerbers.zip  the stand fin alone, for a plain PCB order without parts
The fin sits in the same KiCad file, but JLC counts every separable piece as an extra design (extra fee), so each
piece gets its own file set. The unzipped files stay in production/gerbers_<piece>/ for inspection.

Run with KiCad's bundled Python:  "C:/Program Files/KiCad/10.0/bin/python.exe" export_gerbers.py
Settings follow JLC's KiCad guide: Protel extensions, no X2/netlist attributes, silkscreen clipped by mask openings,
Excellon in mm with decimal coordinates, PTH and NPTH in separate files, oval holes in alternate (G85) format.
"""
import os
import shutil
import subprocess
import sys
import tempfile
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
HW = os.path.normpath(os.path.join(HERE, ".."))
PCB = os.path.join(HW, "pixelpine.kicad_pcb")
OUT = os.path.join(HW, "production")
KICAD_CLI = r"C:/Program Files/KiCad/10.0/bin/kicad-cli.exe"
LAYERS = "F.Cu,B.Cu,F.Paste,B.Paste,F.Silkscreen,B.Silkscreen,F.Mask,B.Mask,Edge.Cuts"
PIECES = ("tree", "stand")


def split(piece, out_path):
    """KiCad-Python step: save a copy of the board holding only one piece. The stand fin lies entirely below
    the trunk (geometry y > HEIGHT), so items are sorted by their bounding box."""
    import pcbnew
    import geometry as g
    from gen_pcb import OY
    kept_alive = []                  # KiCad 10 SWIG breaks if removed items get garbage-collected; keep them

    def drop(board, item):
        board.Remove(item)
        kept_alive.append(item)

    board = pcbnew.LoadBoard(PCB)
    fin_top = pcbnew.FromMM(OY + g.HEIGHT + 0.5)

    def in_fin(item):
        return item.GetBoundingBox().GetTop() > fin_top

    if piece == "tree":
        for d in list(board.GetDrawings()):
            if in_fin(d):
                drop(board, d)
    else:
        for item in list(board.GetFootprints()) + list(board.GetTracks()) + list(board.Zones()):
            drop(board, item)
        for d in list(board.GetDrawings()):
            if not in_fin(d):
                drop(board, d)
    pcbnew.SaveBoard(out_path, board)


def cli(*args):
    r = subprocess.run([KICAD_CLI, *args], capture_output=True, text=True)
    if r.returncode:
        sys.exit(f"kicad-cli failed: {' '.join(args[:3])}\n{r.stdout}{r.stderr}")


def export(piece, tmp):
    board = os.path.join(tmp, f"pixelpine_{piece}.kicad_pcb")
    r = subprocess.run([sys.executable, os.path.abspath(__file__), "--split", piece, board],
                       capture_output=True, text=True)
    if r.returncode or not os.path.exists(board):
        sys.exit(f"split {piece} failed:\n{r.stdout}{r.stderr}")
    out = os.path.join(OUT, f"gerbers_{piece}")
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    cli("pcb", "export", "gerbers", "-o", out, "--layers", LAYERS, "--subtract-soldermask", "--no-x2",
        "--no-netlist", board)
    cli("pcb", "export", "drill", "-o", out + os.sep, "--format", "excellon", "--excellon-units", "mm",
        "--excellon-zeros-format", "decimal", "--excellon-oval-format", "alternate", "--excellon-separate-th",
        "--generate-map", "--map-format", "gerberx2", board)
    for f in os.listdir(out):                    # drop drill files without holes (keep a PTH file in every set)
        if f.endswith(".drl") and "-PTH" not in f and not any(l.startswith("X") for l in open(os.path.join(out, f))):
            os.remove(os.path.join(out, f))
            os.remove(os.path.join(out, f.replace(".drl", "-drl_map.gbr")))
    files = sorted(f for f in os.listdir(out) if not f.endswith(".gbrjob"))
    zpath = os.path.join(OUT, f"pixelpine_{piece}_gerbers.zip")
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as z:
        for f in files:
            z.write(os.path.join(out, f), f)
    print(f"{os.path.basename(zpath)}: {len(files)} files ({', '.join(files)})")
    for side in ("top", "bottom"):               # 3D previews of exactly what went into the Gerbers
        cli("pcb", "render", "-o", os.path.join(OUT, f"preview_{piece}_{side}.png"), "--side", side,
            "-w", "1000", "-h", "1200" if piece == "tree" else "500", "--quality", "high",
            "--background", "opaque", board)


def main():
    if "--split" in sys.argv:
        i = sys.argv.index("--split")
        return split(sys.argv[i + 1], sys.argv[i + 2])
    os.makedirs(OUT, exist_ok=True)
    tmp = tempfile.mkdtemp(prefix="pp_fab_")
    try:
        for piece in PIECES:
            export(piece, tmp)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    main()
