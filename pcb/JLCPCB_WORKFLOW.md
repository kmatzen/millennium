# JLCPCB Fabrication Workflow

## 1. Export Gerbers zip

```bash
./pcb/scripts/export_jlcpcb.sh
```

Creates `pcb/jlcpcb/production_files/GERBER-phonev5.zip` (Gerbers + drills).

## 2. BOM and CPL

Use the **JLCPCB Fabrication Toolkit** plugin in KiCad:
- File → Fabrication Outputs → JLCPCB
- Exports BOM and CPL to `pcb/jlcpcb/production_files/`

## 3. Upload to JLCPCB

Upload from `pcb/jlcpcb/production_files/`:
- GERBER-phonev5.zip
- BOM-phonev5.csv
- CPL-phonev5.csv

See **JLCPCB_COST_REDUCTION.md** for basic vs extended parts and assembly cost optimization.
