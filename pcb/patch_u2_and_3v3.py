#!/usr/bin/env python3
"""
patch_u2_and_3v3.py — programmatic fix of the phonev6 schematic:

  1. Swap U2 from the PT2308-S symbol to a real TDA2822M (correct datasheet
     pinout), using a *position-preserving* symbol so every I/O / power
     connection stays attached by geometry — only the two NF pins change.
  2. Relabel the two mis-labelled GND nets on the NF pins -> NF1 / NF2 and add
     the 100 nF-class feedback caps (NF -> 22 µF -> GND) the TDA2822M needs.
  3. Add the 10 kΩ input-bias resistors (IN -> 10k -> GND) per the datasheet
     stereo application.
  4. Disconnect the two Arduino 3V3 pins from the 3.3V net (leave only the Pi's
     3.3V on TP2), per the user's instruction.

The schematic uses global labels at pin endpoints (no wires), which is what makes
this tractable. Backed up in .edit-backup/; verify with `make verify` afterwards.
"""
import re
import sys
import uuid

SCH = "phonev6.kicad_sch"
ROOT_PATH = "/5a5bcfa4-7dd5-4521-94f3-d918e9453cc6"
PROJECT = "phonev5"

# --- TDA2822M pins at the PT2308-S positions (real datasheet numbering) ---
TDA_PINS = [
    # (type, name, number, x, y, rot)
    ("output",   "OUT1", "1",  10.16,   5.08, 180),
    ("power_in", "Vcc",  "2",   0.0,   11.43, 270),
    ("output",   "OUT2", "3",  10.16,  -5.08, 180),
    ("power_in", "GND",  "4",   0.0,  -11.43,  90),
    ("passive",  "NF2",  "5",  -2.54, -11.43,  90),
    ("input",    "IN2",  "6", -10.16,  -5.08,   0),
    ("input",    "IN1",  "7", -10.16,   5.08,   0),
    ("passive",  "NF1",  "8",   2.54,  11.43, 270),
]


def newid():
    return str(uuid.uuid4())


def block_at(s, start):
    d = 0
    for j in range(start, len(s)):
        if s[j] == "(":
            d += 1
        elif s[j] == ")":
            d -= 1
            if d == 0:
                return start, j + 1
    raise ValueError("unbalanced")


def gl(text, x, y):
    return (
        f'\t(global_label "{text}"\n'
        f'\t\t(shape input)\n\t\t(at {x} {y} 0)\n\t\t(fields_autoplaced yes)\n'
        f'\t\t(effects (font (size 1.27 1.27)) (justify left))\n'
        f'\t\t(uuid "{newid()}")\n'
        f'\t\t(property "Intersheetrefs" "${{INTERSHEET_REFS}}"\n'
        f'\t\t\t(at {x} {y} 0)\n'
        f'\t\t\t(effects (font (size 1.27 1.27)) (justify left) (hide yes))\n\t\t)\n\t)\n'
    )


def passive(libid, ref, value, fp, x, y):
    u1, u2 = newid(), newid()
    return (
        f'\t(symbol\n\t\t(lib_id "{libid}")\n\t\t(at {x} {y} 0)\n\t\t(unit 1)\n'
        f'\t\t(exclude_from_sim no)\n\t\t(in_bom yes)\n\t\t(on_board yes)\n\t\t(dnp no)\n'
        f'\t\t(uuid "{newid()}")\n'
        f'\t\t(property "Reference" "{ref}"\n\t\t\t(at {x + 2.54} {y - 1.27} 0)\n'
        f'\t\t\t(effects (font (size 1.27 1.27)) (justify left))\n\t\t)\n'
        f'\t\t(property "Value" "{value}"\n\t\t\t(at {x + 2.54} {y + 1.27} 0)\n'
        f'\t\t\t(effects (font (size 1.27 1.27)) (justify left))\n\t\t)\n'
        f'\t\t(property "Footprint" "{fp}"\n\t\t\t(at {x} {y} 0)\n'
        f'\t\t\t(effects (font (size 1.27 1.27)) (hide yes))\n\t\t)\n'
        f'\t\t(property "Datasheet" "~"\n\t\t\t(at {x} {y} 0)\n'
        f'\t\t\t(effects (font (size 1.27 1.27)) (hide yes))\n\t\t)\n'
        f'\t\t(pin "1" (uuid "{u1}"))\n\t\t(pin "2" (uuid "{u2}"))\n'
        f'\t\t(instances (project "{PROJECT}" (path "{ROOT_PATH}"\n'
        f'\t\t\t(reference "{ref}") (unit 1))))\n\t)\n'
    )


def no_connect(x, y):
    return f'\t(no_connect (at {x} {y}) (uuid "{newid()}"))\n'


def build_tda_embed(pt_embed):
    """Build the embedded phone:TDA2822M lib_symbol from the PT2308-S embed."""
    e = pt_embed.replace("PT2308-S", "TDA2822M")
    e = e.replace("https://www.lcsc.com/datasheet/C115492.pdf",
                  "https://www.st.com/resource/en/datasheet/cd00000134.pdf")
    # Locate the _1_1 sub-symbol block by paren matching and replace its body.
    i = e.find('(symbol "TDA2822M_1_1"')
    if i < 0:
        sys.exit("could not locate TDA2822M_1_1 sub-symbol in embed")
    rel_start, rel_end = block_at(e, i)
    pins = ""
    for typ, name, num, x, y, rot in TDA_PINS:
        pins += (
            f'\n\t\t\t\t(pin {typ} line (at {x} {y} {rot}) (length 2.54)'
            f' (name "{name}" (effects (font (size 1.27 1.27))))'
            f' (number "{num}" (effects (font (size 1.27 1.27)))))'
        )
    new_sub = '(symbol "TDA2822M_1_1"' + pins + '\n\t\t\t)'
    return e[:rel_start] + new_sub + e[rel_end:]


def main():
    s = open(SCH, encoding="utf-8").read()
    orig = s
    changes = []

    # 1) embed phone:TDA2822M next to PT2308-S
    p = s.find('(symbol "phone:PT2308-S"')
    a, b = block_at(s, p)
    tda_embed = build_tda_embed(s[a:b])
    s = s[:b] + "\n" + tda_embed + s[b:]
    changes.append("embedded phone:TDA2822M lib_symbol")

    # 2) U2 instance: lib_id + Value
    s = s.replace('(lib_id "phone:PT2308-S")', '(lib_id "phone:TDA2822M")', 1)
    s = s.replace('(property "Value" "PT2308-S"', '(property "Value" "TDA2822M"', 1)
    changes.append("U2 -> phone:TDA2822M (lib_id + Value)")

    # 3) relabel the two NF GND nets (match global_label block by coord)
    def relabel(x, y, newtext):
        nonlocal s
        for m in re.finditer(r'\(global_label "GND"', s):
            a, b = block_at(s, m.start())
            blk = s[a:b]
            if re.search(rf'\(at {x} {y} \d+\)', blk):
                s = s[:a] + blk.replace('"GND"', f'"{newtext}"', 1) + s[b:]
                return True
        return False
    assert relabel("115.57", "139.7", "NF1"), "NF1 GND label not found"
    assert relabel("110.49", "162.56", "NF2"), "NF2 GND label not found"
    changes.append("relabelled NF1 (115.57,139.7) and NF2 (110.49,162.56)")

    # 4) remove the two Arduino 3V3 labels, add no-connects there
    removed = 0
    for x, y in [("35.56", "63.5"), ("113.03", "63.5")]:
        for m in re.finditer(r'\(global_label "3\.3V"', s):
            a, b = block_at(s, m.start())
            if re.search(rf'\(at {x} {y} \d+\)', s[a:b]):
                s = s[:a] + s[b:]
                removed += 1
                break
    changes.append(f"removed {removed} Arduino 3V3 labels")

    # 5) build the new parts + labels + no-connects, insert before final ')'
    add = ""
    # input labels on IN1 / IN2 pins
    add += gl("AMP_IN1", "102.87", "146.05")
    add += gl("AMP_IN2", "102.87", "156.21")
    # NF feedback caps: NF -> 22µF -> GND
    add += passive("Device:C_Small", "C13", "22u", "Capacitor_SMD:C_1206_3216Metric", 150.0, 175.0)
    add += gl("NF1", "150.0", "172.46") + gl("GND", "150.0", "177.54")
    add += passive("Device:C_Small", "C14", "22u", "Capacitor_SMD:C_1206_3216Metric", 165.0, 175.0)
    add += gl("NF2", "165.0", "172.46") + gl("GND", "165.0", "177.54")
    # input bias resistors: IN -> 10k -> GND
    add += passive("Device:R_US", "R6", "10k", "Resistor_SMD:R_0805_2012Metric", 135.0, 175.0)
    add += gl("AMP_IN1", "135.0", "171.19") + gl("GND", "135.0", "178.81")
    add += passive("Device:R_US", "R7", "10k", "Resistor_SMD:R_0805_2012Metric", 180.0, 175.0)
    add += gl("AMP_IN2", "180.0", "171.19") + gl("GND", "180.0", "178.81")
    # 3V3 no-connects
    add += no_connect("35.56", "63.5") + no_connect("113.03", "63.5")
    # insert before the final top-level ')'
    last = s.rstrip()
    assert last.endswith(")")
    s = last[:-1] + add + ")\n"
    changes.append("added C13/C14 (22µF NF), R6/R7 (10k bias), input labels, 3V3 no-connects")

    open(SCH, "w", encoding="utf-8").write(s)
    print("Patched", SCH)
    for c in changes:
        print("  -", c)
    print(f"  size {len(orig)} -> {len(s)} bytes")


if __name__ == "__main__":
    main()
