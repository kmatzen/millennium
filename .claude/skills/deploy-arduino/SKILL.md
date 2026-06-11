---
name: deploy-arduino
description: Compile and flash the two Arduino Micro sketches (Alpha=keypad/hook/magstripe, Beta=display/coin/USB-serial) to the phone, using GPIO-reset flashing over SSH through the Pi. Use when asked to deploy/flash/upload Arduino firmware or ship Arduino/ changes to the phone.
---

# Flash the Arduinos (GPIO-reset over SSH)

Two Arduino Micros run the hardware: **Alpha** (`keypad.ino` — keypad matrix,
hook switch, magstripe) and **Beta** (`display.ino` — VFD display, coin
validator, USB-serial bridge). The normal deploy path flashes them *in place*:
the Pi pulses the Arduino RST line via GPIO to drop it into its bootloader,
then `avrdude` flashes over USB. No need to physically connect the Arduino to
the Mac.

- GPIO17 → Alpha (keypad) RST · GPIO27 → Beta (display) RST (open-drain: driven
  low to reset, released to input).
- Requires `raspi-gpio` and `avrdude` on the Pi; the stock Micro bootloader
  enumerates at `/dev/serial/by-id/usb-Arduino_LLC_Arduino_Micro-if00`.

## Steps (run from `Arduino/` on the Mac)

1. **Compile both sketches:**
   ```bash
   cd Arduino && make build
   ```

2. **Flash via the Pi.** The Makefile default `DEPLOY_HOST` is stale
   (`matzen@192.168.86.145`); the Pi's IP is DHCP and has changed — pass the
   current host (prefer the mDNS hostname):
   ```bash
   make deploy          DEPLOY_HOST=matzen@raspberrypi.local   # both, keypad then display
   make deploy_keypad   DEPLOY_HOST=matzen@raspberrypi.local   # Alpha only (GPIO17)
   make deploy_display  DEPLOY_HOST=matzen@raspberrypi.local   # Beta only (GPIO27)
   ```
   Equivalent direct script calls: `./deploy_keypad.sh matzen@raspberrypi.local`
   / `./deploy_display.sh matzen@raspberrypi.local`.

   If `raspberrypi.local` doesn't resolve, find the Pi via the router's DHCP
   table or `arp -a | grep -i 'b8:27\|dc:a6\|e4:5f'`, then pass that IP.

3. **Verify** after flashing — the display should re-init and the daemon should
   resume seeing serial events. Quick check (see the `phone-status` skill):
   `curl -s http://<pi>:80/api/state`, then lift/replace the handset and
   confirm `HU`/`HD` hook events appear in `journalctl -u daemon.service -f`.

## Notes
- `make install` / `make install_keypad` / `make install_display` flash via
  `arduino-cli` when an Arduino is wired **directly to the Mac** — use only for
  bench work, not the in-phone deploy.
- The deploy scripts reset, wait for the bootloader to enumerate, then flash —
  if `avrdude` can't find the port, the GPIO reset may not have dropped the
  board into the bootloader; re-run, or confirm `raspi-gpio` works on the Pi.
