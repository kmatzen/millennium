#!/bin/bash
# Export phonev5 Gerbers and drills to JLCPCB production folder.
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PCB_DIR="$(dirname "$SCRIPT_DIR")"
BOARD="$PCB_DIR/phonev5.kicad_pcb"
OUT="$PCB_DIR/jlcpcb/production_files"
TMP="$PCB_DIR/gerbers_export"
JLCPCB="$PCB_DIR/jlcpcb"

# Clean old outputs before export
rm -f "$OUT/GERBER-phonev5.zip"
rm -rf "$JLCPCB/gerber"

mkdir -p "$OUT" "$TMP"

echo "Exporting Gerbers..."
kicad-cli pcb export gerbers --output "$TMP" --board-plot-params "$BOARD"

echo "Exporting drill files..."
kicad-cli pcb export drill --output "$TMP" --excellon-separate-th "$BOARD"

echo "Creating zip..."
cd "$TMP"
zip -q -r "$OUT/GERBER-phonev5.zip" . -x "*.DS_Store"
cd - >/dev/null
rm -rf "$TMP"

echo "Done. Output: $OUT/GERBER-phonev5.zip"
