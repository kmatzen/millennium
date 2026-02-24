# D2 (PRTR5V0U2X) Schematic Changes: Coin → Speaker Protection

**Rationale:** The coin validator's control board isolates its serial interface from the motors, so D2 on coin_rx provides limited benefit. Moving D2 to protect speaker_front+ (ringer output) is more useful—that output drives the speaker and is vulnerable to back-EMF and ESD.

## Implemented State

- **D2 pin 2 (I/O1):** Connected to `speaker_front+` (TDA2822 channel A output → ringer)
- **D2 pin 3 (I/O2):** Unconnected (available for future use)

### Step-by-Step (KiCad Schematic Editor)

1. **Disconnect D2 from coin_rx**
   - Locate D2 (PRTR5V0U2X) near the coin connector / display Arduino area
   - Find the wire connecting D2 pin 2 (I/O1, left side) to the `coin_rx` net
   - Delete the wire segment that runs from D2 pin 2 to the junction/label at `coin_rx`
   - Ensure `coin_rx` still connects J1 pin 3 to A2 MISO (display Arduino); only D2 is removed from that net

2. **Connect D2 pin 2 to speaker_front+**
   - Draw a wire from D2 pin 2 (I/O1) to a convenient point on the `speaker_front+` net
   - The `speaker_front+` net runs from C_outA (TDA2822 OUT_A) to J2 pin 19 (ringer)
   - You can place a **local label** `speaker_front+` on the new wire, or run the wire to an existing `speaker_front+` label/junction
   - Alternative: add a **global label** `speaker_front+` on the wire from D2 pin 2—that will merge D2 into the existing speaker_front+ net

3. **Optional: Connect D2 pin 3** — D2 I/O2 is currently unconnected. speaker_receiver+ already has D1 I/O2, so D2 I/O2 could be used there for redundancy, or left free for another signal (e.g. J5 mic, magstripe).

4. **Optional: Move D2 for clarity**
   - D2 is currently near the coin connector. Consider moving D2 closer to the TDA2822 (U2) and speaker outputs so the new connections are shorter and easier to follow.

5. **Verify**
   - Run **ERC** (Electrical Rules Check)
   - Export netlist and confirm:
     - `coin_rx` net no longer includes D2
     - `speaker_front+` net includes D2 pin 2
     - D2 pin 3 is unconnected

### Net Reference

| Net        | Nodes                                              |
|------------|----------------------------------------------------|
| coin_rx    | A2 MISO, J1 Pin_3                                  |
| speaker_front+ | C_outA pin 2, J2 Pin_19, **D2 pin 2**          |
| speaker_receiver+ | C_outB pin 2, D1 I/O2, J4 pin 2               |

### D2 I/O2 (free pin) — future options

| Signal         | Benefit                         |
|----------------|---------------------------------|
| speaker_receiver+ | Redundancy (D1 I/O2 already there) |
| J5 mic signal  | ESD from user contact           |
| mag_rdt/rcl    | ESD from card insertion         |
