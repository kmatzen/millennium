# PCB audit and checks
.PHONY: audit erc drc

audit: erc drc
	python3 audit_schematic.py

erc:
	kicad-cli sch erc phonev4.kicad_sch -o erc_report.txt
	@echo "ERC report: erc_report.txt"

drc:
	kicad-cli pcb drc phonev4.kicad_pcb -o drc_report.txt
	@echo "DRC report: drc_report.txt"
