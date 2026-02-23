# 3D-Printed Case

An enclosure for the custom PCB, designed in Blender and printed in PLA+.

## Dimensions

| Measurement        | Value      |
|--------------------|------------|
| Outer size         | 109.5 × 134.0 × 38.0 mm |
| Interior (approx)  | ~103 × 128 mm |
| Wall thickness     | ~3 mm      |
| Depth per half     | 19 mm      |
| PCB size           | 88.9 × 124.5 mm |
| PCB clearance (X)  | ~14.6 mm   |
| PCB clearance (Y)  | ~3.5 mm    |
| Triangle count     | 3,544      |

## Design

The case is a two-piece clamshell that splits horizontally at the midplane:

- **Bottom half** — has rectangular cutouts on one edge for USB cables and
  connectors (2 large + 1 small), allowing cables to pass through to the
  Raspberry Pi and Arduinos while the case is closed.
- **Top half** — plain walls, no cutouts. Acts as the lid.
- **6 notch tabs** — 3 on each long side, designed for rubber bands to hold
  the halves together.

<img src="prusaslicer_screenshot.png" alt="PrusaSlicer view of both halves" style="height:400px;">

## Files

- `case.blend` — Blender source file (editable)
- `case.stl` — Ready-to-print STL mesh (both halves as a single object)
- `prusaslicer_screenshot.png` — Preview of the sliced halves

## Printing

### Preparation

1. Load `case.stl` into your slicer (e.g., PrusaSlicer, Cura, OrcaSlicer).
2. The STL contains the full case as one piece. Use the slicer's cut tool to
   split it at Z = 0 mm (the midplane) into two separate halves.
3. Orient each half with the open face up (flat bottom on the print bed).

### Recommended Settings

| Setting           | Value                |
|-------------------|----------------------|
| Material          | PLA+ (or PETG for heat resistance) |
| Layer height      | 0.2 mm               |
| Infill            | 20–30%               |
| Perimeters        | 3                    |
| Supports          | Not needed (flat bottom, no overhangs) |
| Bed adhesion      | Brim recommended for the larger half |

The original was printed with PLA+ at default slicer settings. No supports
are required since both halves print with the open face up.

### Print Time

Approximately 3–4 hours per half on a standard FDM printer at 0.2 mm layer
height.

## Assembly

1. Place the PCB into the bottom half. The PCB sits on the flat interior
   floor — there are no standoffs or mounting posts.
2. Route cables through the rectangular cutouts: USB cables for the Pi and
   Arduinos, audio cables, and the handset/keypad ribbon cables.
3. Place the top half over the bottom and align the notch tabs.
4. Loop rubber bands around the 3 pairs of opposing notch tabs to clamp the
   halves together.

## Known Limitations

1. **No PCB standoffs**: The PCB rests directly on the case floor. This risks
   shorting traces on the bottom copper layer against any printing artifacts.
   Adding 4 standoff posts (matching the PCB corner positions) would lift the
   board and provide proper mechanical mounting.

2. **No screw bosses**: The halves are held together only by rubber bands.
   Adding M3 screw bosses at the corners would provide a more secure and
   permanent closure.

3. **No ventilation**: The case is fully enclosed with no airflow openings.
   The Pi Zero W runs at ~38°C in this enclosure which is fine, but a few
   small vent slots on the top half would improve airflow if the system is
   under sustained load.

4. **Tight Y clearance**: The PCB has only ~3.5 mm of clearance in the Y
   direction. Components or solder joints that protrude from the board edge
   may interfere with the case walls.

5. **No cable strain relief**: The cutouts are simple rectangular openings
   with no strain relief features. Cables can be pulled out accidentally.

6. **No labeling**: The case has no external markings for connector
   identification or orientation.

## Modification Tips

The Blender source file (`case.blend`) can be edited to address the
limitations above. Common modifications:

- **Add standoffs**: Extrude 4 cylindrical posts (2.5 mm diameter, 3 mm
  tall) from the interior floor at the PCB mounting hole positions.
- **Add screw bosses**: Replace the notch tabs with cylindrical bosses and
  matching holes for M3 × 16 mm screws.
- **Add vents**: Boolean-subtract a row of 2 mm slots from the top half.
- **Add cable guides**: Extrude small channels or clips near the cutouts to
  route and retain cables.

Export the modified model as STL from Blender (File → Export → STL) with
"Selection Only" if you've kept the case as a single object.
