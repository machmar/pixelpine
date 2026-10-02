"""Autoroute pixelpine.kicad_pcb with Freerouting: export Specctra DSN -> Freerouting -> import the session.

Run with KiCad's bundled Python (build_all.py does):  python.exe route.py [max_passes] [--gui] [--keep]
  --gui              open Freerouting's window to watch the tracks being laid (it saves and closes by itself;
                     confirm its "Autorouter Confirmation" dialog to start)
  --keep             resume: keep the board's existing tracks and let the router continue from them
                     (rips up a lot, so only useful for nudging a nearly finished board)
  --import FILE.ses  only import a session saved from Freerouting's window (File > Export Specctra Session
                     File, after stopping the autorouter) into the board; resume later with --keep
If connections stay open the board is routed again from scratch (up to 4 runs, other thread counts) and the best
run is kept.

Needs Freerouting 2.x and a Java 25 runtime unpacked in %LOCALAPPDATA%\\freerouting:
  freerouting-<ver>.jar                 github.com/freerouting/freerouting/releases
  jdk-25.<...>-jre\\bin\\java.exe         github.com/adoptium/temurin25-binaries (JRE zip)
The .dsn, .ses and Freerouting's log are kept in hardware/routing/ for inspection.
"""
import glob
import os
import re
import shutil
import subprocess
import sys
import time

import pcbnew

import geometry as g
from gen_pcb import OY, set_design_rules

HERE = os.path.dirname(os.path.abspath(__file__))
HW = os.path.normpath(os.path.join(HERE, ".."))
PCB = os.path.join(HW, "pixelpine.kicad_pcb")
WORK = os.path.join(HW, "routing")
FR = os.path.join(os.environ.get("LOCALAPPDATA", ""), "freerouting")
# Freerouting is multi-threaded and its result depends a little on thread timing, so a run can end with a stray open
# connection (always in the trunk, never in the LED field). Each retry starts from scratch with another thread count.
ROUTE_MARGIN = 0.005                         # mm of extra clearance while routing: widening the 0.095 mm necks to
                                              # 0.10 mm afterwards eats up to 0.0025 mm per side
ATTEMPT_THREADS = [None, 6, 5, 4]            # None = Freerouting's default (logical cores - 1)

# KiCad 10's SWIG bindings break (every later call returns bare SwigPyObjects) once the Python wrapper of an
# item removed from the board is garbage-collected. Keep removed items referenced until the script ends.
_REMOVED = []


def remove(board, item):
    board.Remove(item)
    _REMOVED.append(item)


def tools():
    jar = sorted(glob.glob(os.path.join(FR, "freerouting-*.jar")))
    java = sorted(glob.glob(os.path.join(FR, "jdk-*-jre", "bin", "java.exe")))
    if not jar or not java:
        sys.exit(f"Freerouting jar and/or Java runtime not found in {FR} (see the docstring of route.py)")
    return java[-1], jar[-1]


def report(board, label):
    board.BuildConnectivity()
    tracks = [t for t in board.GetTracks() if t.GetClass() == "PCB_TRACK"]
    vias = [t for t in board.GetTracks() if t.GetClass() == "PCB_VIA"]
    length = sum(t.GetLength() for t in tracks) / 1e6
    unrouted = board.GetConnectivity().GetUnconnectedCount(False)
    print(f"{label}: {len(tracks)} track segments ({length:.0f} mm), {len(vias)} vias, "
          f"{unrouted} unrouted connections")


def remove_dangling_vias(board):
    """Drop vias connected on fewer than two layers (left over when the router rips up a path)."""
    layers_at = {}
    for t in board.GetTracks():
        if t.GetClass() == "PCB_TRACK":
            for p in (t.GetStart(), t.GetEnd()):
                layers_at.setdefault((t.GetNetCode(), p.x, p.y), set()).add(t.GetLayer())
    for pad in board.GetPads():
        p = pad.GetPosition()
        layers_at.setdefault((pad.GetNetCode(), p.x, p.y), set()).update(
            [pcbnew.F_Cu, pcbnew.B_Cu] if pad.GetAttribute() == pcbnew.PAD_ATTRIB_PTH else [pad.GetLayer()])
    dangling = [v for v in board.GetTracks() if v.GetClass() == "PCB_VIA"
                and len(layers_at.get((v.GetNetCode(), v.GetPosition().x, v.GetPosition().y), ())) < 2]
    for v in dangling:
        remove(board, v)
    return len(dangling)


def widen_thin_tracks(board, min_mm=0.10):
    """Freerouting necks tracks down to ~0.095 mm in tight spots; JLC's 1-2 layer minimum is 0.10 mm."""
    n = 0
    for t in board.GetTracks():
        if t.GetClass() == "PCB_TRACK" and t.GetWidth() < pcbnew.FromMM(min_mm):
            t.SetWidth(pcbnew.FromMM(min_mm))
            n += 1
    return n


def import_into(board, ses, label):
    """Replace the board's wiring with a Freerouting session, clean up, apply the current rules, save."""
    for t in list(board.GetTracks()):
        remove(board, t)
    if not pcbnew.ImportSpecctraSES(board, ses):
        sys.exit(f"SES import failed: {ses}")
    n = remove_dangling_vias(board)
    widen_thin_tracks(board)
    set_design_rules(board)
    board.SynchronizeNetsAndNetClasses(True)
    pcbnew.SaveBoard(PCB, board)
    report(board, label + (f" ({n} dangling vias removed)" if n else ""))


def autoroute(passes, gui, keep, threads=None):
    """One Freerouting run on the saved board; returns the number of connections still unrouted."""
    java, jar = tools()
    os.makedirs(WORK, exist_ok=True)
    dsn, ses, log = (os.path.join(WORK, "pixelpine" + ext) for ext in (".dsn", ".ses", "_freerouting.log"))
    for f in (dsn, ses):
        if os.path.exists(f):
            os.remove(f)

    board = pcbnew.LoadBoard(PCB)
    set_design_rules(board, ROUTE_MARGIN)         # always route with the generator's current rules
    board.SynchronizeNetsAndNetClasses(True)
    if not keep:
        for t in list(board.GetTracks()):         # start clean if the board was routed before
            remove(board, t)
    # export without the stand fin's outline (a separate, copper-free piece): a second boundary in the DSN
    # confuses the router about which outline is the board. The fin edges go back in right after.
    fin_top = pcbnew.FromMM(OY + g.HEIGHT + 0.5)
    fin = [d for d in board.GetDrawings()
           if d.GetLayer() == pcbnew.Edge_Cuts and min(d.GetStart().y, d.GetEnd().y) > fin_top]
    for d in fin:
        board.Remove(d)
    ok = pcbnew.ExportSpecctraDSN(board, dsn)
    for d in fin:
        board.Add(d)
    if not ok:
        sys.exit("DSN export failed (duplicate or empty footprint references break it)")

    t0 = time.time()
    cmd = [java, "-jar", jar, "-de", dsn, "-do", ses, "-mp", str(passes)] + ([] if gui else ["--gui.enabled=false"])
    cmd += ["-mt", str(threads)] if threads else []
    with open(log, "w", encoding="utf8") as f:          # streamed, so progress can be watched while it runs
        f.write(" ".join(cmd) + "\n\n")
        f.flush()
        r = subprocess.run(cmd, stdout=f, stderr=subprocess.STDOUT)
    out = open(log, encoding="utf8", errors="replace").read()
    if r.returncode or not os.path.exists(ses) or not os.path.getsize(ses):
        print(out[-3000:])
        sys.exit(f"Freerouting failed or saved nothing (exit {r.returncode}), log: {log}")

    import_into(board, ses, f"{'resumed' if keep else 'routed'} in {time.time() - t0:.0f} s")
    for l in [l for l in out.splitlines() if re.search(r"(?i)pass|unrouted|complete|score", l)][-3:]:
        print("  freerouting:", l.strip()[:160])
    return board.GetConnectivity().GetUnconnectedCount(False)


def main():
    args = sys.argv[1:]
    if "--import" in args:                        # route.py --import some.ses
        path = os.path.abspath(args[args.index("--import") + 1])
        return import_into(pcbnew.LoadBoard(PCB), path, f"imported {os.path.basename(path)}")
    nums = [a for a in args if a.isdigit()]
    passes = int(nums[0]) if nums else 100
    gui = "--gui" in args                         # show Freerouting's window to watch it route live
    keep = "--keep" in args                       # resume: hand the board's existing tracks to the router
    best_copy, best = PCB + ".best", None
    for attempt, threads in enumerate(ATTEMPT_THREADS):
        left = autoroute(passes, gui, keep and attempt == 0, threads)
        if best is None or left < best:
            best = left
            shutil.copyfile(PCB, best_copy)
        if not left:
            break
        if attempt + 1 < len(ATTEMPT_THREADS):
            print(f"{left} connection(s) still open: routing again from scratch ({attempt + 2}/{len(ATTEMPT_THREADS)})")
    if left > best:
        shutil.copyfile(best_copy, PCB)
        print(f"kept the best run: {best} unrouted connection(s)")
    os.remove(best_copy)


if __name__ == "__main__":
    main()
