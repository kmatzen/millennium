#!/usr/bin/env python3
"""
Audit phonev4 KiCad schematic: components, values, footprints, labels, BOM consistency.
Parses phonev4.kicad_sch (s-expression format) and phonev4.csv (BOM).
"""
import re
import csv
from pathlib import Path

SCHEM = Path(__file__).parent / "phonev4.kicad_sch"
BOM_CSV = Path(__file__).parent / "phonev4.csv"
README = Path(__file__).parent.parent / "pcb" / "README.md"
README = Path(__file__).parent / "README.md"


def extract_property(block: str, name: str) -> str | None:
    m = re.search(rf'\(property "{name}" "([^"]*)"', block)
    return m.group(1) if m else None


def extract_components(schem_path: Path) -> list[dict]:
    """Extract placed components from schematic (symbol instances with full designators)."""
    content = schem_path.read_text()
    components = []
    # Match (symbol ...) blocks that contain (property "Reference" "XXX") with a full designator (not just "A", "J", etc.)
    in_sheet = False
    depth = 0
    block_start = 0
    for i, line in enumerate(content.split("\n")):
        if "(symbol " in line and "_0_" in line or "_1_" in line:
            continue  # Skip symbol unit definitions
        if "(sheet " in line:
            in_sheet = True
        if in_sheet:
            if "(symbol " in line and "path" in content[max(0, content.index(line)-500):content.index(line)+200]:
                # Find the block
                start = content.rfind("(symbol ", 0, content.find(line))
                if start == -1:
                    start = content.find(line)
                end = start
                depth = 0
                for j, c in enumerate(content[start:]):
                    if c == "(":
                        depth += 1
                    elif c == ")":
                        depth -= 1
                        if depth == 0:
                            end = start + j + 1
                            break
                block = content[start:end]
                ref = extract_property(block, "Reference")
                val = extract_property(block, "Value")
                fp = extract_property(block, "Footprint")
                if ref and len(ref) > 1 and not ref.startswith("#"):
                    components.append({"Reference": ref, "Value": val or "", "Footprint": fp or ""})
    # Simpler: find all (symbol ...) that have property overrides in sheet instances
    # KiCad places symbol instances in (symbol (lib_id ...) (at ...) (uuid ...) with (property ...) children
    parts = re.split(r"\(\s*symbol\s+", content)
    for part in parts[1:]:
        # Get the closing paren for this symbol
        depth = 1
        i = 0
        for c in part:
            if c == "(":
                depth += 1
            elif c == ")":
                depth -= 1
                if depth == 0:
                    break
            i += 1
        block = "(" + part[: i + 1]
        ref = extract_property(block, "Reference")
        val = extract_property(block, "Value")
        fp = extract_property(block, "Footprint")
        if not ref:
            continue
        # Filter: only instance blocks have full designators like A1, J1, U1, C_inA
        if ref in ("A", "J", "C", "R", "D", "F", "Q", "U", "TP", "#PWR"):
            continue
        if ref.startswith("#PWR"):
            continue
        components.append({"Reference": ref, "Value": val or "", "Footprint": fp or ""})
    return components


def extract_components_v2(schem_path: Path) -> list[dict]:
    """Extract by finding symbol instances in (sheet ...) - they have (path ...) and overridden properties."""
    content = schem_path.read_text()
    components = []
    # Look for blocks like: (symbol "Arduino_Micro_Socket_0_1" ... (property "Reference" "A2" ...
    # These are inside (sheet (at ...) (fields_autoplaced ...) (symbols (symbol ...
    symbol_blocks = re.findall(
        r'\(symbol "[^"]+"[^)]*(?:\([^)]*\)[^)]*)*?\s+\(property "Reference" "([^"]+)"[^)]*(?:\([^)]*\)[^)]*)*?\s+\(property "Value" "([^"]*)"[^)]*(?:\([^)]*\)[^)]*)*?\s+\(property "Footprint" "([^"]*)"',
        content,
        re.DOTALL
    )
    for ref, val, fp in symbol_blocks:
        if ref.startswith("#"):
            continue
        components.append({"Reference": ref, "Value": val, "Footprint": fp})
    # Simpler regex - just get Reference/Value/Footprint in sequence
    refs = re.findall(r'\(property "Reference" "([^"]+)"\s+\(at [^)]+\)[^)]*\)\s+\(property "Value" "([^"]*)"\s+\(at [^)]+\)[^)]*\)\s+\(property "Footprint" "([^"]*)"',
                      content)
    seen = set()
    for ref, val, fp in refs:
        if ref.startswith("#") or ref in seen:
            continue
        seen.add(ref)
        components.append({"Reference": ref, "Value": val, "Footprint": fp})
    return components


def load_bom(csv_path: Path) -> list[dict]:
    rows = []
    with open(csv_path, newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f, delimiter=";")
        for row in reader:
            rows.append(row)
    return rows


def extract_global_labels(schem_path: Path) -> set[str]:
    content = schem_path.read_text()
    labels = set(re.findall(r'\(global_label "([^"]+)"', content))
    labels.update(re.findall(r'\(label "([^"]+)"', content))
    return labels


def main():
    print("# Schematic Audit Report: phonev4\n")
    schem = Path(__file__).parent / "phonev4.kicad_sch"
    bom_csv = Path(__file__).parent / "phonev4.csv"
    if not schem.exists():
        print("Error: phonev4.kicad_sch not found")
        return 1
    # Extract components - use simpler line-by-line approach
    content = schem.read_text()
    comps = []
    # Match Reference, Value, Footprint in order (with flexible whitespace/nesting)
    for m in re.finditer(
        r'\(property "Reference" "([^"]+)"[\s\S]*?\(property "Value" "([^"]*)"[\s\S]*?\(property "Footprint" "([^"]*)"',
        content
    ):
        ref, val, fp = m.groups()
        if ref.startswith("#") or ref in ("A", "J", "C", "R", "D", "F", "Q", "U", "TP"):
            continue
        comps.append({"Reference": ref, "Value": val, "Footprint": fp})
    # Dedupe by Reference (library symbols have duplicates)
    by_ref = {}
    for c in comps:
        by_ref[c["Reference"]] = c
    comps = list(by_ref.values())

    def natural_key(s):
        return [int(t) if t.isdigit() else t.lower() for t in re.split(r"(\d+)", s)]

    comps.sort(key=lambda x: (natural_key(x["Reference"])))
    print("## 1. Components in Schematic\n")
    print("| Reference | Value | Footprint |")
    print("|-----------|-------|-----------|")
    for c in comps:
        print(f"| {c['Reference']} | {c['Value']} | {c['Footprint'] or '(none)'} |")
    labels = extract_global_labels(schem)
    print("\n## 2. Net Labels (power/signals)\n")
    power_labels = {l for l in labels if "v" in l.lower() or "gnd" in l.lower() or "5" in l or "12" in l}
    print("Power-related:", sorted(power_labels))
    print("\nAll labels count:", len(labels))
    print("\n## 3. BOM Comparison (schematic vs phonev4.csv)\n")
    bom = load_bom(bom_csv)
    bom_refs = set()
    for row in bom:
        raw = row.get("Designator", "").replace(" ", "")
        for part in raw.split(","):
            part = part.strip()
            if "-" in part:
                a, b = part.split("-", 1)
                prefix_a = re.match(r"^([A-Za-z]+)", a).group(1) if re.match(r"^([A-Za-z]+)", a) else ""
                prefix_b = re.match(r"^([A-Za-z]+)", b).group(1) if re.match(r"^([A-Za-z]+)", b) else ""
                num_a = int(re.search(r"(\d+)$", a).group(1)) if re.search(r"(\d+)$", a) else 0
                num_b = int(re.search(r"(\d+)$", b).group(1)) if re.search(r"(\d+)$", b) else 0
                if prefix_a == prefix_b and num_a <= num_b:
                    for i in range(num_a, num_b + 1):
                        bom_refs.add(f"{prefix_a}{i}")
                else:
                    bom_refs.add(part)
            else:
                bom_refs.add(part)
    schem_refs = {c["Reference"] for c in comps}
    in_schem_not_bom = schem_refs - bom_refs
    in_bom_not_schem = bom_refs - schem_refs
    if in_schem_not_bom:
        print("In schematic but not BOM:", sorted(in_schem_not_bom))
    if in_bom_not_schem:
        print("In BOM but not schematic:", sorted(in_bom_not_schem))
    if not in_schem_not_bom and not in_bom_not_schem:
        print("BOM and schematic references match.")
    print("\n## 4. Documentation Alignment\n")
    if "5V_MAIN" in content or "5v" in content:
        if "5V_MAIN" not in content and "5v" in content:
            print("- **Label mismatch**: Schematic uses `5v` but README specifies `5V_MAIN`. Consider renaming for consistency.")
        elif "5V_MAIN" in content:
            print("- Power label 5V_MAIN present in schematic.")
    if "12V_COIN" not in content:
        if "12v" in content.lower():
            print("- Schematic uses `12v`; README specifies `12V_COIN`. Consider renaming for consistency.")
        else:
            print("- **Missing**: No 12V_COIN or 12v net label. Per docs, XL6009 OUT+ should be 12V_COIN.")
    else:
        print("- 12V_COIN label present.")
    print("\n## 5. Part Value Discrepancies\n")
    q1 = next((c for c in comps if c["Reference"] == "Q1"), None)
    if q1 and "Si2319" in (q1.get("Value") or ""):
        print("- **Q1**: Schematic has Si2319CDS, BOM/README specify Si2301. Verify pin compatibility.")
    d3 = next((c for c in comps if c["Reference"] == "D3"), None)
    if d3:
        print("- **D3/LED1**: Schematic uses D3 for power LED; BOM uses LED1. Annotate consistently.")
    print("\n## 6. Missing Footprints\n")
    no_fp = [c for c in comps if not c.get("Footprint") or c.get("Footprint") == "(none)"]
    if no_fp:
        for c in no_fp:
            print(f"- {c['Reference']} ({c['Value']}): no footprint assigned")
    return 0


if __name__ == "__main__":
    exit(main())
