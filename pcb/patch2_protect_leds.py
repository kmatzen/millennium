#!/usr/bin/env python3
"""
patch2_protect_leds.py — add protection, filtering, and status LEDs to phonev6.

  Protection:  input TVS on 5V, TVS on both speaker outputs, fuse 1A->2A.
  Filtering:   boost (XL6009) input + output bulk/decoupling caps,
               Zobel (4.7 + 0.1µF) on each speaker output, output coupling
               caps 100µF->220µF.
  Status LEDs: 3.3V rail, 12V rail, Alpha reset, Beta reset, I2C SDA, I2C SCL.

All new parts are hand-solderable SMD and connect via global labels (the sheet's
connectivity model). Run after patch_u2_and_3v3.py; verify with `make verify`.
"""
import re
import sys
import uuid

SCH = "phonev6.kicad_sch"
SYM = "phone.kicad_sym"
ROOT_PATH = "/5a5bcfa4-7dd5-4521-94f3-d918e9453cc6"
PROJECT = "phonev5"

# pin offsets (symbol coords, Y-up) per lib_id: (pin1 (x,y), pin2 (x,y))
PIN_OFF = {
    "Device:C_Small": ((0, 2.54), (0, -2.54)),
    "Device:R_US":    ((0, 3.81), (0, -3.81)),
    "Device:LED":     ((-3.81, 0), (3.81, 0)),   # pin1=K, pin2=A
    "phone:P6KE6.8CA": ((-3.81, 0), (3.81, 0)),
}

# (lib_id, ref, value, footprint, net@pin1, net@pin2)
PARTS = [
    # --- protection (TVS: bidirectional, net on pin1, GND on pin2) ---
    ("phone:P6KE6.8CA", "D5", "P6KE6.8CA", "Diode_SMD:D_SMB", "5V_MAIN", "GND"),
    ("phone:P6KE6.8CA", "D6", "P6KE6.8CA", "Diode_SMD:D_SMB", "speaker_front+", "GND"),
    ("phone:P6KE6.8CA", "D7", "P6KE6.8CA", "Diode_SMD:D_SMB", "speaker_receiver+", "GND"),
    # --- boost (XL6009) input + output filtering ---
    ("Device:C_Small", "C15", "47u",  "Capacitor_SMD:C_1210_3225Metric", "BOOST_IN+", "GND"),
    ("Device:C_Small", "C16", "0.1u", "Capacitor_SMD:C_0805_2012Metric", "BOOST_IN+", "GND"),
    ("Device:C_Small", "C17", "22u",  "Capacitor_SMD:C_1210_3225Metric", "12V_COIN", "GND"),
    ("Device:C_Small", "C18", "0.1u", "Capacitor_SMD:C_0805_2012Metric", "12V_COIN", "GND"),
    # --- Zobel networks on speaker outputs (R speaker+ -> node, C node -> GND) ---
    ("Device:R_US",    "R8", "4.7",  "Resistor_SMD:R_0805_2012Metric", "speaker_front+", "ZOBEL1"),
    ("Device:C_Small", "C19", "0.1u", "Capacitor_SMD:C_0805_2012Metric", "ZOBEL1", "GND"),
    ("Device:R_US",    "R9", "4.7",  "Resistor_SMD:R_0805_2012Metric", "speaker_receiver+", "ZOBEL2"),
    ("Device:C_Small", "C20", "0.1u", "Capacitor_SMD:C_0805_2012Metric", "ZOBEL2", "GND"),
    # --- status LEDs (R source->node ; LED pin1=K=sink pin2=A=node) ---
    ("Device:R_US", "R10", "330",  "Resistor_SMD:R_0805_2012Metric", "3.3V", "LED_3V3"),
    ("Device:LED",  "D8",  "grn",  "LED_SMD:LED_0805_2012Metric", "GND", "LED_3V3"),
    ("Device:R_US", "R11", "3.3k", "Resistor_SMD:R_0805_2012Metric", "12V_COIN", "LED_12V"),
    ("Device:LED",  "D9",  "grn",  "LED_SMD:LED_0805_2012Metric", "GND", "LED_12V"),
    ("Device:R_US", "R12", "1k",   "Resistor_SMD:R_0805_2012Metric", "5V_MAIN", "LED_RST1"),
    ("Device:LED",  "D10", "ylw",  "LED_SMD:LED_0805_2012Metric", "RESET1", "LED_RST1"),
    ("Device:R_US", "R13", "1k",   "Resistor_SMD:R_0805_2012Metric", "5V_MAIN", "LED_RST2"),
    ("Device:LED",  "D11", "ylw",  "LED_SMD:LED_0805_2012Metric", "RESET2", "LED_RST2"),
    ("Device:R_US", "R14", "1k",   "Resistor_SMD:R_0805_2012Metric", "5V_MAIN", "LED_SDA"),
    ("Device:LED",  "D12", "blu",  "LED_SMD:LED_0805_2012Metric", "sda", "LED_SDA"),
    ("Device:R_US", "R15", "1k",   "Resistor_SMD:R_0805_2012Metric", "5V_MAIN", "LED_SCL"),
    ("Device:LED",  "D13", "blu",  "LED_SMD:LED_0805_2012Metric", "scl", "LED_SCL"),
]


def nid():
    return str(uuid.uuid4())


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


def gl(text, x, y):
    return (
        f'\t(global_label "{text}" (shape input) (at {x} {y} 0) (fields_autoplaced yes)\n'
        f'\t\t(effects (font (size 1.27 1.27)) (justify left)) (uuid "{nid()}")\n'
        f'\t\t(property "Intersheetrefs" "${{INTERSHEET_REFS}}" (at {x} {y} 0)\n'
        f'\t\t\t(effects (font (size 1.27 1.27)) (justify left) (hide yes))))\n'
    )


def part(lib, ref, value, fp, x, y, n1, n2):
    (p1x, p1y), (p2x, p2y) = PIN_OFF[lib]
    s1x, s1y = x + p1x, y - p1y          # sheet coords (Y flips)
    s2x, s2y = x + p2x, y - p2y
    inst = (
        f'\t(symbol (lib_id "{lib}") (at {x} {y} 0) (unit 1)\n'
        f'\t\t(exclude_from_sim no) (in_bom yes) (on_board yes) (dnp no) (uuid "{nid()}")\n'
        f'\t\t(property "Reference" "{ref}" (at {x + 3.0} {y - 1.27} 0)\n'
        f'\t\t\t(effects (font (size 1.27 1.27)) (justify left)))\n'
        f'\t\t(property "Value" "{value}" (at {x + 3.0} {y + 1.27} 0)\n'
        f'\t\t\t(effects (font (size 1.27 1.27)) (justify left)))\n'
        f'\t\t(property "Footprint" "{fp}" (at {x} {y} 0)\n'
        f'\t\t\t(effects (font (size 1.27 1.27)) (hide yes)))\n'
        f'\t\t(property "Datasheet" "~" (at {x} {y} 0)\n'
        f'\t\t\t(effects (font (size 1.27 1.27)) (hide yes)))\n'
        f'\t\t(pin "1" (uuid "{nid()}")) (pin "2" (uuid "{nid()}"))\n'
        f'\t\t(instances (project "{PROJECT}" (path "{ROOT_PATH}" (reference "{ref}") (unit 1)))))\n'
    )
    return inst + gl(n1, s1x, s1y) + gl(n2, s2x, s2y)


def embed_p6ke(s):
    """Embed phone:P6KE6.8CA into lib_symbols from phone.kicad_sym."""
    if '(symbol "phone:P6KE6.8CA"' in s:
        return s
    sym = open(SYM, encoding="utf-8").read()
    i = sym.find('(symbol "P6KE6.8CA"')
    a, b = block_at(sym, i)
    embed = sym[a:b].replace('(symbol "P6KE6.8CA"', '(symbol "phone:P6KE6.8CA"', 1)
    # insert after the PT2308-S embed (stable anchor inside lib_symbols)
    p = s.find('(symbol "phone:PT2308-S"')
    _, pe = block_at(s, p)
    return s[:pe] + "\n\t\t" + embed + s[pe:]


def set_value(s, ref, newval):
    """Set the Value property of instance <ref>."""
    i = s.find(f'"Reference" "{ref}"')
    st = s.rfind("(symbol", 0, i)
    a, b = block_at(s, st)
    blk = s[a:b]
    blk2 = re.sub(r'(\(property "Value" ")[^"]*(")', rf'\g<1>{newval}\g<2>', blk, count=1)
    return s[:a] + blk2 + s[b:]


def main():
    s = open(SCH, encoding="utf-8").read()
    s = embed_p6ke(s)
    # value changes
    s = set_value(s, "F1", "2A")
    s = set_value(s, "C11", "220µF 16V")
    s = set_value(s, "C12", "220µF 16V")
    # place new parts in a grid in empty space (y >= 188)
    add = ""
    x0, y0, dx, dy, cols = 118.0, 188.0, 19.05, 15.24, 6
    for k, (lib, ref, val, fp, n1, n2) in enumerate(PARTS):
        x = x0 + (k % cols) * dx
        y = y0 + (k // cols) * dy
        add += part(lib, ref, val, fp, x, y, n1, n2)
    last = s.rstrip()
    assert last.endswith(")")
    s = last[:-1] + add + ")\n"
    open(SCH, "w", encoding="utf-8").write(s)
    print(f"Added {len(PARTS)} parts; F1->2A; C11/C12->220µF. New size {len(s)} bytes.")


if __name__ == "__main__":
    main()
