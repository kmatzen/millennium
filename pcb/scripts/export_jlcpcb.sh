#!/bin/bash
# Export phonev5 Gerbers and drills to a zip.
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PCB_DIR="$(dirname "$SCRIPT_DIR")"
BOARD="$PCB_DIR/phonev5.kicad_pcb"
OUT="$PCB_DIR/gerbers"
TMP="$PCB_DIR/gerbers_export"

mkdir -p "$TMP"

echo "Exporting Gerbers..."
kicad-cli pcb export gerbers --output "$TMP" --board-plot-params "$BOARD"

echo "Exporting drill files..."
kicad-cli pcb export drill --output "$TMP" --excellon-separate-th "$BOARD"

echo "Creating zip..."
cd "$TMP"
zip -q -r "$OUT/phonev5-gerbers.zip" . -x "*.DS_Store"
cd - >/dev/null
rm -rf "$TMP"

# Also copy to JLCPCB production folder so one export serves both
JLCPCB_DIR="$PCB_DIR/jlcpcb/production_files"
mkdir -p "$JLCPCB_DIR"
cp "$OUT/phonev5-gerbers.zip" "$JLCPCB_DIR/GERBER-phonev5.zip"

echo "Done. Output: $OUT/phonev5-gerbers.zip"
echo "Also: $JLCPCB_DIR/GERBER-phonev5.zip"
