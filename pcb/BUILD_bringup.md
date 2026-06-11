# phonev6 — bench bring-up checklist

Walk this top-to-bottom. **Do not skip stages** — each one catches a class of fault before the next stage exposes more expensive parts to it.

## Stage 0 — visual + polarity

- [ ] All solder joints reflowed cleanly; no bridges; no cold joints (loupe).
- [ ] **Polarity** of every electrolytic / LED / SOT-23 / diode matches `BUILD_polarity.md` (silkscreen mark ↔ schematic net at marked pin).
- [ ] Run `make verify` — netlist sync, pinouts, footprints, ERC, DRC all green.

## Stage 1 — bare board, no power, no ICs populated

Use a DMM in resistance / continuity mode. **Do not install the Pi or Arduinos yet.** With the board on the bench, GND clip on TP3:

- [ ] `5V_MAIN` ↔ `GND`: ≥ ~1 kΩ (no short). Probe: TP1.
- [ ] `BOOST_IN+` ↔ `GND`: ≥ ~1 kΩ (no short). Probe: (probe at any cap pin on this net).
- [ ] `12V_COIN` ↔ `GND`: ≥ ~1 kΩ (no short). Probe: (probe at any cap pin on this net).
- [ ] `3.3V` ↔ `GND`: ≥ ~1 kΩ (no short). Probe: TP2.
- [ ] Audio outputs `speaker_front+` ↔ `GND` and `speaker_receiver+` ↔ `GND`: should read open / high (the output coupling caps look like an open in DC).

If anything reads a hard short, **stop** and find it (look for solder bridges under SOIC pins, polarized parts in wrong, TVS shorted).

## Stage 2 — first power-on, current-limited, ICs still out

Bench supply: **5 V at 200 mA limit**. Power leads only into the 5 V input. Pi and Arduinos still **out of their sockets**.

- [ ] Power-on current draw: a few mA (just the LEDs + leakage). Supply does **not** hit the current limit.
- [ ] **F1** does not trip (no flashing red on the supply).
- [ ] **D4** (red power LED) lights.
- [ ] Voltage at `5V_MAIN` ≈ 5.0 V. Probe: TP1.
- [ ] Voltage at `BOOST_IN+` ≈ 5 V (≈ V_in − Q1 R_DS_on × I; should be well within ~50 mV of `5V_MAIN`). If `BOOST_IN+` is near 0 V, Q1 is backwards or the gate is floating.
- [ ] **D9** (green 12 V LED) lights ⇒ XL6009 is switching.
- [ ] `12V_COIN` ≈ 12 V (trim the XL6009 module's pot if not). Probe at any cap on this net (e.g. C17/C18).

## Stage 3 — populate the Pi (A3), bump current limit

Power **off**. Plug the Pi into its socket. Raise supply limit to ~1.5 A.

- [ ] Power on. Total current ≤ ~500 mA at idle on the Zero 2 W.
- [ ] **D8** (green 3.3 V LED) lights.
- [ ] Voltage at `3.3V` ≈ 3.3 V. Probe: TP2.
- [ ] Pi boots normally (activity LED blinking after a few seconds; SSH becomes reachable on the network).

## Stage 4 — populate Arduinos (A1 keypad, A2 display)

Power off. Plug in the two Arduino Micros.

- [ ] Power on. Total current increases by ~50 mA × 2 (Arduino idle).
- [ ] Flash both via the **`deploy-arduino`** workflow (`make deploy_keypad`, `make deploy_display`).
- [ ] **D10 / D11** (yellow `RESET1` / `RESET2` LEDs) **flash briefly** during the GPIO reset pulse — visible during the deploy. If they don't flash, the Pi GPIO isn't toggling the reset line.
- [ ] **D12 / D13** (blue `SDA` / `SCL`) flicker once the daemon runs I2C between Alpha and Beta. At idle they're off (lines pulled high); during traffic they twitch.

## Stage 5 — verify the audio amp (U2)

With everything powered, **no audio signal** yet:

- [ ] DC voltage at **U2 pin 2 (Vcc)** ≈ 5.0 V.
- [ ] DC voltage at **U2 pin 4 (GND)** ≈ 0 V (solid ground).
- [ ] DC voltage at **U2 pin 1 (OUT1)** and **pin 3 (OUT2)** ≈ ½·Vcc (~2.5 V). If either output sits near 0 V or near 5 V, the bias is broken — most likely the NF cap (C13 on pin 8, C14 on pin 5) is missing / reversed / cold-jointed.
- [ ] DC voltage at **pin 8 (NF1)** and **pin 5 (NF2)** ≈ ½·Vcc, stable. AC ripple here ⇒ bad / wrong NF cap.

Now apply a tone (Pi-side, after the daemon is up):

```bash
speaker-test -D out_left_solo  -t sine -f 1000   # ringer channel (OUT1)
speaker-test -D out_right_solo -t sine -f 1000   # earpiece channel (OUT2)
```

- [ ] Scope on **U2 pin 1 / pin 3** shows a clean sine, no clipping at moderate volume.
- [ ] Scope on `speaker_front+` / `speaker_receiver+` (after the 100 µF output cap) shows AC sine centred on 0 V into the speaker load.
- [ ] Scope on **`5V_MAIN`** AC-coupled while a loud tone plays: ripple ≪ 100 mV. Heavy ripple means insufficient bulk decoupling or boost noise.

## Stage 6 — functional end-to-end

- [ ] Lift / replace handset: hook events arrive at the daemon (`journalctl -u daemon.service -f` shows `HU` / `HD`).
- [ ] Keypad: pressing keys logs `K<char>` events.
- [ ] Coin: drop a known coin, verify a `V<byte>` event with the expected value.
- [ ] Magstripe: swipe a card, daemon logs `C<PAN>`.
- [ ] Place a test call (matches the **`phone-status`** skill: `curl -s http://<pi>:8081/api/state` should show `sip_registered:1`).

