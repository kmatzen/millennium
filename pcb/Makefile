# phonev6 PCB verification + manufacturing pipeline.
# `make verify` must be all-green before ordering.

PROJ ?= phonev6
SCH  := $(PROJ).kicad_sch
PCB  := $(PROJ).kicad_pcb

# Point kicad-cli at the installed KiCad libraries so ERC/DRC resolve symbols
# and footprints (otherwise every part reports "library not found").
export KICAD9_SYMBOL_DIR    := /Applications/KiCad/KiCad.app/Contents/SharedSupport/symbols
export KICAD9_FOOTPRINT_DIR := /Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints
export KICAD9_3DMODEL_DIR   := /Applications/KiCad/KiCad.app/Contents/SharedSupport/3dmodels

.PHONY: verify erc drc netlist-sync footprint-parity pinouts footprints \
        polarity bringup build-docs audit clean-reports gerbers

# Full guardrail suite — the gate before reordering.
verify: netlist-sync footprint-parity pinouts footprints erc drc
	@echo ""
	@echo "================ ALL GUARDRAILS PASSED ================"

# v5-killer: every schematic connection must exist on the board.
netlist-sync:
	@python3 check_netlist_sync.py $(SCH) $(PCB)

# Catch schematic↔board footprint drift (e.g. shrunk a cap but didn't re-sync).
footprint-parity:
	@python3 check_footprint_parity.py $(SCH) $(PCB)

# U2-bug-killer: critical ICs match their datasheet pinout.
pinouts:
	@python3 check_pinouts.py $(SCH)

# Keep everything hand-solderable.
footprints:
	@python3 check_footprints.py $(SCH)

# Hand-assembly aids — regenerate the polarity sheet + bring-up checklist.
build-docs: polarity bringup
polarity:
	@python3 gen_polarity_sheet.py $(SCH)
bringup:
	@python3 gen_bringup_checklist.py $(SCH)

# Schematic electrical rules (errors gate the build; full report saved).
erc:
	@kicad-cli sch erc $(SCH) -o erc_report.txt --severity-error --exit-code-violations \
	  && echo "✓ ERC: no errors" \
	  || (echo "✗ ERC errors — see erc_report.txt"; exit 1)

# Board design rules incl. unrouted/clearance (errors gate the build).
drc:
	@kicad-cli pcb drc $(PCB) -o drc_report.txt --severity-error --exit-code-violations \
	  && echo "✓ DRC: no errors" \
	  || (echo "✗ DRC errors — see drc_report.txt"; exit 1)

# Legacy regex audit (kept for the BOM/value cross-check).
audit:
	@python3 audit_schematic.py

# JLCPCB manufacturing outputs (gerbers, drill, BOM, CPL).
gerbers:
	@bash scripts/export_jlcpcb.sh $(PROJ)

clean-reports:
	@rm -f erc_report.txt drc_report.txt
