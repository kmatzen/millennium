#!/bin/bash
# Export <project> Gerbers, drills, BOM, and CPL for JLCPCB.
# Usage: export_jlcpcb.sh [project]   (default: phonev6)
# Gerbers/drill/pos all reference the drill/place origin so JLCPCB aligns them.
# Run after `make verify` is all-green.
set -e

PROJ="${1:-phonev6}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PCB_DIR="$(dirname "$SCRIPT_DIR")"
BOARD="$PCB_DIR/$PROJ.kicad_pcb"
SCH="$PCB_DIR/$PROJ.kicad_sch"
OUT="$PCB_DIR/jlcpcb/production_files"
TMP="$PCB_DIR/gerbers_export"

[ -f "$BOARD" ] || { echo "Board not found: $BOARD"; exit 1; }

rm -f "$OUT/GERBER-$PROJ.zip" "$OUT/BOM-$PROJ.csv" "$OUT/CPL-$PROJ.csv"
rm -rf "$TMP"
mkdir -p "$OUT" "$TMP"

echo "Gerbers..."
kicad-cli pcb export gerbers --output "$TMP" \
  --layers "F.Cu,In1.Cu,In2.Cu,B.Cu,F.Paste,B.Paste,F.Silkscreen,B.Silkscreen,F.Mask,B.Mask,Edge.Cuts" \
  --use-drill-file-origin --no-protel-ext "$BOARD"

echo "Drill..."
kicad-cli pcb export drill --output "$TMP/" --format excellon \
  --drill-origin plot --excellon-separate-th --generate-map --map-format gerberx2 "$BOARD"

echo "Zip gerbers+drill -> GERBER-$PROJ.zip ..."
( cd "$TMP" && zip -q -r "$OUT/GERBER-$PROJ.zip" . -x "*.DS_Store" )
rm -rf "$TMP"

echo "CPL (component placement)..."
kicad-cli pcb export pos --output "$OUT/CPL-$PROJ.csv.raw" \
  --format csv --units mm --side both --use-drill-file-origin --exclude-dnp "$BOARD"
# Reshape to the strict JLCPCB CPL: Designator,Mid X,Mid Y,Layer,Rotation with Layer
# capitalized (Top/Bottom).  KiCad emits Designator,Val,Package,Mid X,Mid Y,Rotation,Layer
# in lower-case top/bottom — JLCPCB rejects that on upload.
python3 - "$OUT/CPL-$PROJ.csv.raw" "$OUT/CPL-$PROJ.csv" << 'PYEOF'
import csv, sys, re
# kicad-cli 9 pos --format csv writes: Ref,Val,Package,PosX,PosY,Rot,Side
# JLCPCB wants:                       Designator,Mid X,Mid Y,Layer,Rotation
# Layer values must be Top/Bottom (capitalized) — KiCad emits lowercase.
#
# Rotation corrections — KiCad's footprint origin/orientation vs JLCPCB's part-
# library orientation can differ for certain packages. Empirical offsets per ref
# pattern (degrees added to the KiCad rotation). Adjust if a preview still looks
# wrong (flip the sign on a problem ref).
ROT_OFFSETS = [
    (re.compile(r'^J\d'),  -90),   # all J* connectors: 90° CW
    (re.compile(r'^U2$'),  -90),   # TDA2822M SOIC-8:    90° CW
]

def rot_offset(ref):
    for pat, off in ROT_OFFSETS:
        if pat.match(ref):
            return off
    return 0

with open(sys.argv[1], newline='') as f:
    rows = list(csv.DictReader(f))
with open(sys.argv[2], 'w', newline='') as f:
    w = csv.writer(f)
    w.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
    for r in rows:
        ref = r["Ref"]
        layer = r["Side"].strip().capitalize()
        rot = (float(r["Rot"]) + rot_offset(ref)) % 360
        w.writerow([ref, r["PosX"], r["PosY"], layer, f"{rot:.4f}"])
PYEOF
rm -f "$OUT/CPL-$PROJ.csv.raw"

echo "BOM..."
kicad-cli sch export bom --output "$OUT/BOM-$PROJ.csv" \
  --fields 'Value,Reference,Footprint,${QUANTITY},LCSC' \
  --labels 'Comment,Designator,Footprint,Quantity,LCSC Part #' \
  --group-by 'Value,Footprint,LCSC' --exclude-dnp "$SCH"

echo ""
echo "Done. JLCPCB files in $OUT/:"
ls -la "$OUT/GERBER-$PROJ.zip" "$OUT/BOM-$PROJ.csv" "$OUT/CPL-$PROJ.csv"
