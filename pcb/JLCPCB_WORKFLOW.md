# JLCPCB Fabrication Workflow

## 1. Export Gerbers zip

```bash
./pcb/scripts/export_jlcpcb.sh
```

Creates `pcb/jlcpcb/production_files/GERBER-phonev6.zip` (Gerbers + drills).

## 2. BOM and CPL

Use the **JLCPCB Fabrication Toolkit** plugin in KiCad:
- File → Fabrication Outputs → JLCPCB
- Exports BOM and CPL to `pcb/jlcpcb/production_files/`

## 3. Upload to JLCPCB

Upload from `pcb/jlcpcb/production_files/`:
- GERBER-phonev6.zip
- BOM-phonev6.csv
- CPL-phonev6.csv

See **JLCPCB_COST_REDUCTION.md** for basic vs extended parts and assembly cost optimization.
