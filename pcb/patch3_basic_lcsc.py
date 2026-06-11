#!/usr/bin/env python3
"""
patch3_basic_lcsc.py — assign JLCPCB *basic* LCSC parts where possible.

Strategy: prefer basic parts (no per-extended fee). Where shrinking the
footprint hits a basic part, switch (the user invited this).

Decisions:
  * 22µF MLCC -> 0805 / C45783 (Samsung 25V X5R) — basic; covers C13/C14
    (NF), C15 (boost input), C17 (boost output) on a single BOM line.
    Footprint shrinks 1206/1210 -> 0805 — board needs another re-route.
  * 100µF 16V electrolytic (C37308) is already basic — revert C11/C12 from
    220µF -> 100µF (no footprint change, no re-route for those caps).
  * 0.1µF 0805 / C49678 (already basic, used as-is).
  * All resistors are basic UNI-ROYAL 0805.
  * LEDs: green C2297, yellow C2296, blue C2293 — all basic, 0805.
  * TVS: keep value P6KE6.8CA / footprint D_SMB / C78395 (the production
    part for D_SMB TVS; verify basic at order — may be extended).
  * U2 TDA2822M SOP-8 / C725341 — EXTENDED (no basic dual amp; one-time fee).

Run after patch2.  Then in KiCad: Update PCB from Schematic, re-route the
shrunken caps, `make verify`, `make gerbers`.
"""
import re
import sys

SCH = "phonev6.kicad_sch"

# ref -> (value, footprint, lcsc)
ASSIGN = {
    # amp
    "U2":  ("TDA2822M",  "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm", "C725341"),
    # 100µF electrolytics (revert 220µF -> 100µF to use basic C37308)
    "C11": ("100µF 16V", "Capacitor_SMD:CP_Elec_6.3x5.4", "C37308"),
    "C12": ("100µF 16V", "Capacitor_SMD:CP_Elec_6.3x5.4", "C37308"),
    # 22µF MLCCs shrunk to 0805 to use basic C45783
    "C13": ("22u", "Capacitor_SMD:C_0805_2012Metric", "C45783"),
    "C14": ("22u", "Capacitor_SMD:C_0805_2012Metric", "C45783"),
    "C15": ("22u", "Capacitor_SMD:C_0805_2012Metric", "C45783"),
    "C17": ("22u", "Capacitor_SMD:C_0805_2012Metric", "C45783"),
    # 0.1µF (already 0805 / basic)
    "C16": ("0.1u", "Capacitor_SMD:C_0805_2012Metric", "C49678"),
    "C18": ("0.1u", "Capacitor_SMD:C_0805_2012Metric", "C49678"),
    "C19": ("0.1u", "Capacitor_SMD:C_0805_2012Metric", "C49678"),
    "C20": ("0.1u", "Capacitor_SMD:C_0805_2012Metric", "C49678"),
    # resistors (all UNI-ROYAL 0805 1%)
    "R6":  ("10k",  "Resistor_SMD:R_0805_2012Metric", "C17414"),
    "R7":  ("10k",  "Resistor_SMD:R_0805_2012Metric", "C17414"),
    # Zobel 4.7Ω: previously listed C17557 by mistake (which is actually 220Ω,
    # UNI-ROYAL 0805W8F2200T5E).  Real 4.7Ω 0805 basic: C2889556 (VO, 5% — fine
    # for Zobel damping). If R8/R9 are unpopulated the amp still works.
    "R8":  ("4.7",  "Resistor_SMD:R_0805_2012Metric", "C2889556"),
    "R9":  ("4.7",  "Resistor_SMD:R_0805_2012Metric", "C2889556"),
    "R10": ("330",  "Resistor_SMD:R_0805_2012Metric", "C17630"),
    "R11": ("3.3k", "Resistor_SMD:R_0805_2012Metric", "C26010"),   # verify basic
    "R12": ("1k",   "Resistor_SMD:R_0805_2012Metric", "C17513"),
    "R13": ("1k",   "Resistor_SMD:R_0805_2012Metric", "C17513"),
    "R14": ("1k",   "Resistor_SMD:R_0805_2012Metric", "C17513"),
    "R15": ("1k",   "Resistor_SMD:R_0805_2012Metric", "C17513"),
    # TVS (bidirectional SMB) — keep production part C78395; verify basic at order
    "D5":  ("P6KE6.8CA", "Diode_SMD:D_SMB", "C78395"),
    "D6":  ("P6KE6.8CA", "Diode_SMD:D_SMB", "C78395"),
    "D7":  ("P6KE6.8CA", "Diode_SMD:D_SMB", "C78395"),
    # LEDs (Hubei KENTO basic colors)
    "D8":  ("Green LED",  "LED_SMD:LED_0805_2012Metric", "C2297"),
    "D9":  ("Green LED",  "LED_SMD:LED_0805_2012Metric", "C2297"),
    "D10": ("Yellow LED", "LED_SMD:LED_0805_2012Metric", "C2296"),
    "D11": ("Yellow LED", "LED_SMD:LED_0805_2012Metric", "C2296"),
    "D12": ("Blue LED",   "LED_SMD:LED_0805_2012Metric", "C2293"),
    "D13": ("Blue LED",   "LED_SMD:LED_0805_2012Metric", "C2293"),
}


def block_at(s, i):
    d = 0
    for j in range(i, len(s)):
        if s[j] == "(":
            d += 1
        elif s[j] == ")":
            d -= 1
            if d == 0:
                return i, j + 1
    raise ValueError


def find_instance(s, ref):
    """Return (start,end) span of the symbol instance for <ref>."""
    i = s.find(f'"Reference" "{ref}"')
    if i < 0:
        return None
    st = s.rfind("(symbol", 0, i)
    return block_at(s, st)


def set_or_add_prop(blk, name, value):
    """Update an existing (property "name" "...") or insert a new one before the first (pin."""
    m = re.search(rf'(\(property "{re.escape(name)}" ")[^"]*(")', blk)
    if m:
        return blk[:m.start(1)] + m.group(1) + value + m.group(2) + blk[m.end():]
    # insert a new property block right before the first '(pin '
    pin = blk.find("(pin ")
    indent = "\t\t"
    prop = (f'{indent}(property "{name}" "{value}"\n'
            f'{indent}\t(at 0 0 0)\n'
            f'{indent}\t(effects (font (size 1.27 1.27)) (hide yes))\n'
            f'{indent})\n{indent}')
    return blk[:pin] + prop + blk[pin:]


def main():
    s = open(SCH, encoding="utf-8").read()
    changed = 0
    for ref, (val, fp, lcsc) in ASSIGN.items():
        span = find_instance(s, ref)
        if not span:
            print(f"  ? {ref} not found"); continue
        a, b = span
        blk = s[a:b]
        new = set_or_add_prop(blk, "Value", val)
        new = set_or_add_prop(new, "Footprint", fp)
        new = set_or_add_prop(new, "LCSC", lcsc)
        if new != blk:
            s = s[:a] + new + s[b:]
            changed += 1
    open(SCH, "w", encoding="utf-8").write(s)
    print(f"Updated {changed}/{len(ASSIGN)} parts with basic LCSC + footprint.")


if __name__ == "__main__":
    main()
