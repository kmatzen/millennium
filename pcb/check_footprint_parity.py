#!/usr/bin/env python3
"""
check_footprint_parity.py — verify schematic and board agree on each component's
footprint. Net-level sync (check_netlist_sync.py) can pass while the board still
carries an out-of-date footprint (e.g. schematic shrunk a cap from 1206 to 0805
but the user didn't re-run "Update PCB from Schematic"). This catches that.

Usage: check_footprint_parity.py <sch> <pcb>
Exit 0 = all footprints match; 1 = mismatch.
"""
import os
import re
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

KICAD_LIBS = {
    "KICAD9_SYMBOL_DIR":    "/Applications/KiCad/KiCad.app/Contents/SharedSupport/symbols",
    "KICAD9_FOOTPRINT_DIR": "/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints",
}


def sch_footprints(sch):
    out = tempfile.mktemp(suffix=".xml")
    env = dict(os.environ, **KICAD_LIBS)
    r = subprocess.run(
        ["kicad-cli", "sch", "export", "netlist", sch, "-o", out,
         "--format", "kicadxml"],
        env=env, capture_output=True, text=True,
    )
    if r.returncode != 0:
        sys.exit(f"netlist export failed:\n{r.stderr}")
    fp = {}
    for c in ET.parse(out).getroot().find("components"):
        f = c.find("footprint")
        fp[c.get("ref")] = f.text if f is not None and f.text else ""
    return fp


def board_footprints(pcb):
    s = open(pcb, encoding="utf-8").read()
    fp = {}
    i = 0
    while True:
        i = s.find("(footprint ", i)
        if i == -1:
            break
        # capture lib:fp inside the (footprint "...") quotes
        m = re.match(r'\(footprint\s+"([^"]+)"', s[i:i + 200])
        libfp = m.group(1) if m else ""
        # paren-match the block to find Reference
        d, j = 0, i
        while j < len(s):
            if s[j] == "(":
                d += 1
            elif s[j] == ")":
                d -= 1
                if d == 0:
                    break
            j += 1
        ref = re.search(r'\(property "Reference" "([^"]+)"', s[i:j + 1])
        if ref:
            fp[ref.group(1)] = libfp
        i = j + 1
    return fp


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    sch = sch_footprints(sys.argv[1])
    pcb = board_footprints(sys.argv[2])
    bad = []
    for ref, sfp in sch.items():
        bfp = pcb.get(ref)
        if bfp is None:
            bad.append((ref, sfp, "<not on board>"))
        elif sfp and sfp != bfp:
            bad.append((ref, sfp, bfp))
    print("=== Footprint parity: schematic ↔ board ===")
    if bad:
        print(f"\n✗ {len(bad)} component(s) with footprint drift:")
        for ref, sfp, bfp in sorted(bad):
            print(f"    {ref}:  sch={sfp}")
            print(f"          brd={bfp}")
        print("\nFix: in KiCad, Tools → Update PCB from Schematic, then re-route.")
        return 1
    print(f"\n✓ {len(sch)} components — schematic and board footprints match.\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
