#!/usr/bin/env python3
"""Add no_connect markers for pin_not_connected ERC violations."""
import re
import uuid
import sys

ERC_FILE = "erc_report.txt"
SCH_FILE = "phonev4.kicad_sch"

def parse_erc_pins():
    content = open(ERC_FILE).read()
    matches = re.findall(
        r'\[pin_not_connected\].*?@\(([0-9.]+) mm, ([0-9.]+) mm\)',
        content, re.DOTALL
    )
    return [(float(x), float(y)) for x, y in matches]

def add_no_connects(sch_path, coords):
    with open(sch_path) as f:
        content = f.read()

    # Find insertion point: after last junction, before first wire
    wire_match = re.search(r'\n\t\(wire\n', content)
    if not wire_match:
        print("Could not find wire section")
        return False

    insert_pos = wire_match.start()

    # Build no_connect entries
    entries = []
    for x, y in coords:
        uid = str(uuid.uuid4())
        entries.append(f'''\t(no_connect
		(at {x} {y})
		(uuid "{uid}")
	)''')

    block = "\n" + "\n".join(entries) + "\n\t"
    content = content[:insert_pos] + block + content[insert_pos:]
    with open(sch_path, "w") as f:
        f.write(content)
    return True

def main():
    coords = parse_erc_pins()
    if not coords:
        print("No pin_not_connected violations found in ERC report")
        print("Run: kicad-cli sch erc phonev4.kicad_sch -o erc_report.txt")
        return 1

    print(f"Adding {len(coords)} no_connect markers...")
    if add_no_connects(SCH_FILE, coords):
        print("Done. Run ERC again to verify.")
        return 0
    return 1

if __name__ == "__main__":
    sys.exit(main())
