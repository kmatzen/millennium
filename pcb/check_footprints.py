#!/usr/bin/env python3
"""
check_footprints.py — guard that every part stays hand-solderable.

We author in SMD but assemble by hand, so flag anything too small/fine-pitch:
  - passives smaller than 0805 (i.e. 0603/0402/0201),
  - leadless/fine-pitch IC packages (QFN/DFN/BGA/WLCSP/SON/LGA),
  - lead pitch finer than 0.65 mm.
Through-hole parts and connectors always pass.

Usage: check_footprints.py <project.kicad_sch>
Exit 0 = all hand-solderable, 1 = something too small.
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

MIN_IMPERIAL = "0805"
IMPERIAL_ORDER = ["0201", "0402", "0603", "0805", "1206", "1210", "2010", "2512"]
LEADLESS = re.compile(r"\b(QFN|DFN|BGA|WLCSP|CSP|SON|LGA|VQFN|UQFN|UDFN|VDFN)\b", re.I)
PITCH = re.compile(r"P0?\.(\d+)mm", re.I)


def load_footprints(sch_path):
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
    out_list = []
    for comp in root.find("components"):
        ref = comp.get("ref")
        fp_el = comp.find("footprint")
        fp = fp_el.text if fp_el is not None and fp_el.text else ""
        out_list.append((ref, fp))
    return out_list


def classify(fp):
    """Return a reason string if the footprint is too small, else None."""
    base = fp.split(":")[-1]
    if base == "":
        return "no footprint assigned"
    # Through-hole / connectors are always fine.
    if re.search(r"(THT|_TH_|Vertical|Horizontal|DIP|Socket|RJ9|AUDIO|IDC|Radial|Axial|TestPoint|Module|Pin_)", base, re.I):
        if not LEADLESS.search(base):
            # still check passive size for THT? THT passives are large; skip.
            pass
    # Passive imperial size (R_0805_..., C_0402_..., LED_0603_...).
    m = re.search(r"_(\d{4})_\d+Metric", base) or re.match(r"[A-Z]+_(\d{4})", base)
    if m and m.group(1) in IMPERIAL_ORDER:
        if IMPERIAL_ORDER.index(m.group(1)) < IMPERIAL_ORDER.index(MIN_IMPERIAL):
            return f"passive {m.group(1)} smaller than {MIN_IMPERIAL}"
    # Leadless / fine-pitch IC packages.
    if LEADLESS.search(base):
        return f"leadless/fine-pitch package ({base})"
    pm = PITCH.search(base)
    if pm:
        # P0.65mm -> "65", P0.5mm -> "5" or "50"; normalize to mm.
        digits = pm.group(1)
        mm = float("0." + digits) if len(digits) == 1 else float("0." + digits.ljust(2, "0"))
        if mm < 0.65:
            return f"lead pitch {mm} mm finer than 0.65 mm"
    return None


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    print("=== Hand-solderable footprint guard (min passive %s) ===" % MIN_IMPERIAL)
    bad = []
    for ref, fp in load_footprints(sys.argv[1]):
        reason = classify(fp)
        if reason:
            bad.append((ref, fp, reason))
    if bad:
        print(f"\n✗ {len(bad)} part(s) too small / fine-pitch for hand soldering:")
        for ref, fp, reason in sorted(bad):
            print(f"    {ref}: {fp or '(none)'} — {reason}")
        print()
        return 1
    print("\n✓ all parts are hand-solderable.\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
