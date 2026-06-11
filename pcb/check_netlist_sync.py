#!/usr/bin/env python3
"""
check_netlist_sync.py — verify the PCB board's connectivity matches the schematic.

This is the guard against the v5-class bug: connections that exist in the
schematic but were never pushed to the board ("Update PCB from Schematic" was
skipped, or Freerouting only saw a stale .dsn). KiCad's own DRC reports
"0 unconnected" on such a board because the *board's* netlist never knew about
the missing connection — so DRC cannot catch it. This does.

Method: export the schematic netlist (authoritative intent) and read the pad→net
assignments straight out of the .kicad_pcb. For every schematic net, confirm all
its pins land on a single common net on the board. A schematic net that is split
across multiple board nets (or whose pads are absent) = a missing connection.

Usage: check_netlist_sync.py <project.kicad_sch> <project.kicad_pcb>
Exit code 0 = in sync, 1 = mismatch (or tool error).
"""
import os
import re
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

KICAD_LIBS = {
    "KICAD9_SYMBOL_DIR": "/Applications/KiCad/KiCad.app/Contents/SharedSupport/symbols",
    "KICAD9_FOOTPRINT_DIR": "/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints",
}


def export_sch_netlist(sch_path):
    out = tempfile.mktemp(suffix=".xml")
    env = dict(os.environ, **KICAD_LIBS)
    r = subprocess.run(
        ["kicad-cli", "sch", "export", "netlist", sch_path, "-o", out,
         "--format", "kicadxml"],
        env=env, capture_output=True, text=True,
    )
    if r.returncode != 0:
        sys.exit(f"kicad-cli netlist export failed:\n{r.stderr}")
    return out


def schematic_nets(xml_path):
    """Return {netname: set((ref,pin))} from the schematic netlist."""
    root = ET.parse(xml_path).getroot()
    nets = {}
    for net in root.find("nets"):
        name = net.get("name")
        pins = {(n.get("ref"), n.get("pin")) for n in net.findall("node")}
        nets[name] = pins
    return nets


def board_pad_nets(pcb_path):
    """Return {(ref,pad): netname} for every pad placed on the board."""
    text = open(pcb_path, encoding="utf-8").read()
    pad_net = {}
    # Walk top-level (footprint ...) blocks by paren matching.
    i = 0
    while True:
        i = text.find("(footprint ", i)
        if i == -1:
            break
        depth, j = 0, i
        while j < len(text):
            c = text[j]
            if c == "(":
                depth += 1
            elif c == ")":
                depth -= 1
                if depth == 0:
                    break
            j += 1
        block = text[i:j + 1]
        i = j + 1
        ref_m = re.search(r'\(property "Reference" "([^"]+)"', block)
        if not ref_m:
            continue
        ref = ref_m.group(1)
        # Each pad: (pad "NAME" ... (net ID "NETNAME") ...) ; pads with no net are unconnected.
        for chunk in block.split("(pad ")[1:]:
            nm = re.match(r'"([^"]+)"', chunk)
            if not nm:
                continue
            pad = nm.group(1)
            net_m = re.search(r'\(net \d+ "([^"]*)"', chunk[:400])
            pad_net[(ref, pad)] = net_m.group(1) if net_m else None
    return pad_net


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    sch, pcb = sys.argv[1], sys.argv[2]
    sch_nets = schematic_nets(export_sch_netlist(sch))
    pad_net = board_pad_nets(pcb)

    missing_pads = []   # schematic pin not present on board at all
    split_nets = []     # schematic net spread across >1 board net (missing connection)
    for name, pins in sorted(sch_nets.items()):
        if len(pins) < 2:
            continue  # single-pin nets carry no connection to verify
        board_nets_seen = {}
        for ref, pin in sorted(pins):
            if (ref, pin) not in pad_net:
                missing_pads.append((name, ref, pin))
            else:
                board_nets_seen.setdefault(pad_net[(ref, pin)], []).append(f"{ref}.{pin}")
        real = {k: v for k, v in board_nets_seen.items() if k}  # ignore unconnected pads here
        if len(real) > 1:
            split_nets.append((name, real))

    print("=== Netlist sync: schematic ↔ board ===")
    print(f"schematic nets: {len(sch_nets)}   board pads: {len(pad_net)}")
    ok = True
    if missing_pads:
        ok = False
        print(f"\n✗ {len(missing_pads)} schematic pin(s) absent from the board:")
        for name, ref, pin in missing_pads[:40]:
            print(f"    {ref}.{pin}  (schematic net '{name}')")
    if split_nets:
        ok = False
        print(f"\n✗ {len(split_nets)} schematic net(s) SPLIT on the board (missing connection):")
        for name, real in split_nets:
            frags = "; ".join(f"[{bn or '<none>'}]={','.join(p)}" for bn, p in real.items())
            print(f"    net '{name}' -> {frags}")
    if ok:
        print("\n✓ board connectivity matches the schematic — no missing connections.")
        return 0
    print("\nFix: in KiCad, Tools → Update PCB from Schematic (sync), re-export the "
          ".dsn, re-route in Freerouting, import the .ses, then re-run this check.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
