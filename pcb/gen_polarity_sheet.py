#!/usr/bin/env python3
"""
gen_polarity_sheet.py — emit a markdown sheet of every polarized part on the
board and the net at each polarity-bearing pin, so you can eyeball orientation
against silkscreen during hand assembly.

Usage: gen_polarity_sheet.py <sch>            # writes BUILD_polarity.md
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

# (criterion → (label, pin-function table)).  pin-function table: pin_number → human label
POLARIZED = [
    # (matcher, kind, pin_labels_dict)
    (lambda lib, fp, val: "CP_Elec" in fp or "CP_Radial" in fp or "CP_Axial" in fp,
     "electrolytic cap", {"1": "+ (anode)", "2": "− (cathode)"}),
    (lambda lib, fp, val: lib == "Device:LED",
     "LED",              {"1": "K  (cathode, marked)", "2": "A  (anode)"}),
    (lambda lib, fp, val: lib == "phone:IRF9540_THT" or "MOSFET" in val.upper(),
     "MOSFET (SOT-23)",  {"1": "G  (gate)", "2": "S  (source)", "3": "D  (drain)"}),
    (lambda lib, fp, val: lib == "Device:D" or "Schottky" in val,
     "diode",            {"1": "K  (cathode, marked)", "2": "A  (anode)"}),
]


def load(sch):
    out = tempfile.mktemp(suffix=".xml")
    env = dict(os.environ, **KICAD_LIBS)
    subprocess.run(
        ["kicad-cli", "sch", "export", "netlist", sch, "-o", out, "--format", "kicadxml"],
        env=env, capture_output=True, text=True, check=True,
    )
    root = ET.parse(out).getroot()
    comps = {}
    for c in root.find("components"):
        ref = c.get("ref")
        lib = (c.find("libsource").get("lib") + ":" + c.find("libsource").get("part")
               if c.find("libsource") is not None else "")
        fp = c.findtext("footprint") or ""
        val = c.findtext("value") or ""
        comps[ref] = {"lib": lib, "fp": fp, "val": val, "pins": {}}
    for net in root.find("nets"):
        for n in net.findall("node"):
            r = n.get("ref")
            if r in comps:
                comps[r]["pins"][n.get("pin")] = net.get("name")
    return comps


def classify(c):
    for matcher, kind, pin_labels in POLARIZED:
        if matcher(c["lib"], c["fp"], c["val"]):
            return kind, pin_labels
    return None, None


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    comps = load(sys.argv[1])
    groups = {}
    for ref, c in comps.items():
        kind, pin_labels = classify(c)
        if not kind:
            continue
        rows = []
        for pin, lbl in pin_labels.items():
            net = c["pins"].get(pin, "(unconnected)")
            rows.append((pin, lbl, net))
        groups.setdefault(kind, []).append((ref, c["val"], c["fp"].split(":")[-1], rows))

    lines = [
        "# phonev6 — Hand-assembly polarity sheet",
        "",
        "Auto-generated from the schematic. For each part, verify the **silkscreen "
        "polarity mark** (bar, dot, plus, beveled corner, pin-1 chamfer) lines up "
        "with the **net listed at the marked pin** before reflow / iron.",
        "",
    ]

    def natkey(s):
        return [int(t) if t.isdigit() else t for t in re.split(r"(\d+)", s)]

    for kind in ("electrolytic cap", "diode", "LED", "MOSFET (SOT-23)"):
        if kind not in groups:
            continue
        lines.append(f"\n## {kind}\n")
        lines.append("| Ref | Value | Package | Pin | Function | Net |")
        lines.append("|-----|-------|---------|-----|----------|-----|")
        for ref, val, fp, rows in sorted(groups[kind], key=lambda x: natkey(x[0])):
            for i, (pin, lbl, net) in enumerate(rows):
                head = f"`{ref}` | {val} | {fp}" if i == 0 else " | | "
                lines.append(f"| {head} | {pin} | {lbl} | `{net}` |")

    open("BUILD_polarity.md", "w").write("\n".join(lines) + "\n")
    print("wrote BUILD_polarity.md")
    counts = ", ".join(f"{len(v)} {k}" for k, v in groups.items())
    print(f"  polarized parts: {counts}")


if __name__ == "__main__":
    main()
