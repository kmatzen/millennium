#!/usr/bin/env python3
"""
check_pinouts.py — validate critical IC symbols against their datasheet pinouts.

This is the guard against the v5 amp disaster: U2's symbol used a made-up pinout
(power/ground/IO on the wrong pins) that matched no real part, so the board was
routed wrong yet passed ERC/DRC. This validates each listed part's pins against a
datasheet truth table:
  - the symbol's pin FUNCTION name per pin number, and
  - that supply/ground pins land on the correct power nets.

Add a part by dropping its datasheet pinout into EXPECTED below.

Usage: check_pinouts.py <project.kicad_sch>
Exit 0 = all listed parts match, 1 = mismatch.
"""
import os
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

KICAD_LIBS = {
    "KICAD9_SYMBOL_DIR": "/Applications/KiCad/KiCad.app/Contents/SharedSupport/symbols",
    "KICAD9_FOOTPRINT_DIR": "/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints",
}

# Datasheet truth tables. Per pin number: (function-name substring expected in the
# symbol, required net or None). Net "GND" matches GND; give the exact rail name.
EXPECTED = {
    "U2": {
        "part": "TDA2822M (UTC, SOP-8)",
        "pins": {
            "1": ("OUT", None),       # OUTPUT 1
            "2": ("Vcc", "5V_MAIN"),  # supply
            "3": ("OUT", None),       # OUTPUT 2
            "4": ("GND", "GND"),      # ground
            "5": ("NF", None),        # NF2 (feedback 2)
            "6": ("IN", None),        # INPUT 2
            "7": ("IN", None),        # INPUT 1
            "8": ("NF", None),        # NF1 (feedback 1)
        },
    },
    # XL6009 boost module: 1=IN+, 2=IN-, 3=OUT-, 4=OUT+  (per phonev6 netlist)
    # IN+ sits on the protected rail (5V_MAIN -> F1 fuse -> Q1 reverse-polarity
    # P-FET -> BOOST_IN+), so don't pin it to raw 5V_MAIN.
    "U1": {
        "part": "XL6009 boost module",
        "pins": {
            "1": ("IN", None),
            "2": ("IN", "GND"),
            "3": ("OUT", "GND"),
            "4": ("OUT", "12V_COIN"),
        },
    },
}


def load_pins(sch_path):
    """Return {ref: {pin: (net, pinfunction)}} from the schematic netlist."""
    out = tempfile.mktemp(suffix=".xml")
    env = dict(os.environ, **KICAD_LIBS)
    r = subprocess.run(
        ["kicad-cli", "sch", "export", "netlist", sch_path, "-o", out,
         "--format", "kicadxml"],
        env=env, capture_output=True, text=True,
    )
    if r.returncode != 0:
        sys.exit(f"kicad-cli netlist export failed:\n{r.stderr}")
    root = ET.parse(out).getroot()
    pins = {}
    for net in root.find("nets"):
        name = net.get("name")
        for n in net.findall("node"):
            pins.setdefault(n.get("ref"), {})[n.get("pin")] = (name, n.get("pinfunction") or "")
    return pins


def net_role(net):
    if net in ("GND", "gnd"):
        return "GND"
    return net


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    pins = load_pins(sys.argv[1])
    print("=== Pinout validation vs datasheet ===")
    ok = True
    for ref, spec in EXPECTED.items():
        if ref not in pins:
            print(f"\n• {ref} ({spec['part']}): not placed in schematic — skipped")
            continue
        actual = pins[ref]
        problems = []
        for pin, (fn_expect, net_req) in spec["pins"].items():
            net, fn = actual.get(pin, (None, None))
            if net is None:
                problems.append(f"pin {pin}: expected {fn_expect} but pin not in netlist")
                continue
            if fn_expect.lower() not in fn.lower():
                problems.append(f"pin {pin}: symbol fn '{fn}' ≠ datasheet '{fn_expect}'")
            if net_req and net_role(net) != net_role(net_req):
                problems.append(f"pin {pin} ({fn_expect}): on net '{net}', must be '{net_req}'")
        if problems:
            ok = False
            print(f"\n✗ {ref} ({spec['part']}):")
            for p in problems:
                print(f"    {p}")
        else:
            print(f"\n✓ {ref} ({spec['part']}): pins match datasheet")
    print()
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
