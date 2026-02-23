# Hardware Assembly Reference

Physical hardware inside the Millennium payphone, how everything connects,
and notes from the current build.

## Components

| Component                    | Model / Part             | Notes                           |
|------------------------------|--------------------------|---------------------------------|
| Single-board computer        | Raspberry Pi Zero W      | Single USB port, needs hub      |
| Keypad microcontroller       | Arduino Micro (custom)   | "Millennium Alpha" board def    |
| Display microcontroller      | Arduino Micro (custom)   | "Millennium Beta" board def     |
| USB audio card               | C-Media CM109 (Unitek Y-247A) | USB class-compliant, stereo out + mono mic in |
| USB hub                      | Huasheng USB2.0 HUB      | 2 hubs daisy-chained for 3 ports |
| Boost converter              | XL6009 module            | Converts phone line voltage to 5V |
| Custom PCB                   | phonev4                  | Connects all peripherals        |
| VFD display                  | Noritake CU20026SCPB-T23A | 20×2 character VFD             |
| Coin validator               | Original Millennium part | 600 baud serial protocol        |
| Magstripe reader             | Original Millennium part | Clock + data signals            |
| Handset                      | Original Millennium part | RJ9 connector (4P4C)            |
| Ringer speaker               | Original Millennium part | Front-mounted                   |
| Keypad                       | Original Millennium part | 4×7 matrix, 20-pin ribbon       |
| 3D-printed case              | PLA+                     | Houses the PCB                  |

## USB Topology

The Raspberry Pi Zero W has a single micro-USB OTG port. A USB hub provides
connectivity for all USB peripherals:

```
Pi Zero W (USB OTG)
  └─ USB Hub #1
       ├─ USB Hub #2
       │    ├─ Arduino "Millennium Alpha" (keypad)
       │    └─ Arduino "Millennium Beta" (display)
       └─ C-Media USB Audio Adapter
```

The two Arduinos enumerate as USB serial devices:
- `/dev/serial/by-id/usb-Arduino_LLC_Millennium_Alpha-if00`
- `/dev/serial/by-id/usb-Arduino_LLC_Millennium_Beta-if00`

The USB audio card appears as ALSA `hw:1,0`.

## Cable Routing

Inside the payphone, cables route from the original hardware to the custom PCB:

| Cable              | From                | To                     | Connector     |
|--------------------|---------------------|------------------------|---------------|
| Keypad ribbon      | Keypad matrix       | PCB J2 (2×10 header)   | 20-pin IDC    |
| Display ribbon     | VFD display         | PCB J3 (2×13 header)   | 26-pin IDC    |
| Coin validator     | Coin mechanism      | PCB J1 (2×5 header)    | 10-pin IDC    |
| Magstripe reader   | Card slot           | PCB J6 (2×7 header)    | 14-pin IDC    |
| Handset            | Handset cord        | PCB J4 (RJ9 jack)      | 4P4C RJ9      |
| Mic audio          | USB audio card out  | PCB J5 (3.5mm jack)    | 3.5mm stereo  |
| Speaker audio      | USB audio card out  | PCB J7 (3.5mm jack)    | 3.5mm stereo  |
| USB (display)      | PCB Arduino Beta    | Pi via USB hub          | Micro-USB     |
| USB (audio)        | USB audio card      | Pi via USB hub          | USB-A         |
| Power              | Phone line pair     | PCB XL6009 input        | Screw terminal |

## Power

Phone line voltage (typically 48V DC on-hook, varies) enters the XL6009
boost converter module which outputs regulated 5V. The 5V rail powers
everything: both Arduinos, the Raspberry Pi (via GPIO 5V pins), the VFD
display logic, and the audio amplifier.

Total estimated current draw:

| Component              | Current (typical) |
|------------------------|-------------------|
| Raspberry Pi Zero W    | 150 mA            |
| Arduino Micro × 2      | 50 mA each        |
| VFD display            | 100 mA            |
| USB audio card         | 50 mA             |
| Coin validator         | 50 mA             |
| Audio amplifier        | 50–200 mA (playing) |
| USB hub                | 50 mA             |
| **Total**              | **~550–700 mA**   |

## Thermal

The Pi Zero W runs at approximately 38°C inside the closed case with no
active cooling. This is well within operating limits (throttling starts at
80°C). The phone's metal enclosure acts as a passive heat sink.

## Physical Mounting

The custom PCB sits inside a 3D-printed PLA+ case (see `case/README.md`).
The case mounts inside the payphone's internal cavity. The original
Millennium control board is removed and replaced with this assembly.

Original hardware (keypad, display, coin validator, magstripe reader, handset,
ringer) remains in place and connects to the PCB via ribbon cables and the
RJ9/3.5mm jacks.

## Serial Numbers and Device IDs

| Device           | USB VID:PID  | Serial Name          |
|------------------|--------------|----------------------|
| Keypad Arduino   | 2341:8045    | Millennium Alpha     |
| Display Arduino  | 2341:8046    | Millennium Beta      |
| USB Audio        | 0d8c:0014    | Audio Adapter        |

The custom Arduino board definitions assign unique VID/PID pairs so the
Arduinos can be identified by name in `/dev/serial/by-id/`.
