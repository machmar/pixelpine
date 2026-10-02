"""Verify the exported schematic netlist matches the charlieplex map in geometry.py.

Usage: kicad-cli sch export netlist --format kicadsexpr -o pp.net pixelpine.kicad_sch
       python check_netlist.py pp.net
"""
import re
import sys

import geometry as g


def parse(path):
    s = open(path, encoding="utf8").read()
    s = s[s.index("(nets"):]
    nets = {}
    for chunk in s.split("(net\n")[1:]:
        name = re.search(r'\(name "([^"]*)"\)', chunk).group(1).lstrip("/")
        nets[name] = set(re.findall(r'\(ref "([^"]+)"\)\s*\(pin "([^"]+)"\)', chunk))
    return nets


def main(path):
    nets = parse(path)
    net_of = {node: name for name, nodes in nets.items() for node in nodes}
    ok = True
    for i, (pname, pin) in enumerate(g.LINE_PINS):
        nodes = nets.get(f"L{i}", set())
        want = {(f"D{n + 1}", "2") for n in range(g.N_LEDS) if g.charlie(n)[0] == i}
        want |= {(f"D{n + 1}", "1") for n in range(g.N_LEDS) if g.charlie(n)[1] == i}
        want |= {(f"R{i + 1}", "2")}
        if nodes != want:
            ok = False
            print(f"L{i}: mismatch, extra={nodes - want} missing={want - nodes}")
        mcu_net = nets.get(net_of.get(("U1", str(pin))), set())
        want_mcu = {("U1", str(pin)), (f"R{i + 1}", "1")}
        if pname.endswith("SWDIO"):
            want_mcu.add(("TP3", "1"))                   # programming pads sit on the MCU side of the resistor
        if pname.endswith("SWCLK"):
            want_mcu.add(("TP5", "1"))
        if mcu_net != want_mcu:
            ok = False
            print(f"{pname} (U1.{pin}): {sorted(mcu_net)}")
    for name in ("+5V", "GND", "CC1", "CC2"):
        print(f"{name:8s} -> {sorted(f'{r}.{p}' for r, p in nets.get(name, []))}")
    # point-to-point nets, found by pin (they are plain wires, no label)
    expect = [{("U1", str(g.NRST_PIN[1])), ("TP6", "1")},     # NRST -> programming pad
              {("U1", str(g.TOUCH_PIN[1])), ("R19", "1")},  # touch ADC pin -> ESD resistor
              {("R19", "2"), ("TP4", "1")},            # ESD resistor -> star
              {("J1", "A5"), ("R17", "1")}, {("J1", "B5"), ("R18", "1")}]
    for want in expect:
        got = nets.get(net_of.get(next(iter(want))), set())
        if got != want:
            ok = False
            print(f"expected net {sorted(want)}, got {sorted(got)}")
    print("unconnected-*:", [n for n in nets if n.startswith("unconnected")])
    print("CHARLIEPLEX NETLIST OK" if ok else "NETLIST MISMATCH")
    return ok


if __name__ == "__main__":
    sys.exit(0 if main(sys.argv[1]) else 1)
