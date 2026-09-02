# Owner Wi-Fi onboarding

## Decision

Use a temporary captive portal backed by `hostapd`, `dnsmasq`, and the existing
`wpa_supplicant`/`dhcpcd` network stack. Do not migrate deployed Bullseye phones
to NetworkManager solely for onboarding.

Each phone advertises `Millennium-Setup-<device-id>` only when it has never had
a configured network or when an authenticated physical recovery gesture opens
a 15-minute setup window. The WPA2 setup passphrase is random per device and is
printed as a Wi-Fi QR code in the sealed owner packet. A shared, known password
would let any nearby person reconfigure every phone and is not acceptable.

## Owner flow

1. The phone displays `WIFI SETUP` and the setup SSID.
2. The owner scans the printed QR code or joins the setup network manually.
3. The operating system opens the captive portal. `http://setup.millennium/`
   remains available if automatic detection does not fire.
4. The portal scans for nearby networks, while also allowing a hidden SSID.
5. The owner selects a network, enters its passphrase, and presses Connect.
6. The phone writes a candidate root-only network configuration atomically,
   leaves AP mode, and attempts station mode.
7. Success requires an address, default route, DNS resolution, the signed OTA
   endpoint, and an outbound maintenance check-in. The handset and portal say
   `CONNECTED`; no account or cloud registration is required.
8. On failure, the prior working configuration is restored and the setup AP
   returns with a plain-language error. Entered credentials are discarded from
   process memory as soon as practical and are never logged.

## Security boundary

Setup mode is not an administration network. Its firewall permits only DHCP,
DNS, and HTTP/HTTPS to the local portal on the setup interface. It blocks SSH,
port 8081, forwarding, access to the LAN, and all other device services. The
portal runs as an unprivileged user and hands one validated candidate to a
small root helper over a Unix socket. The helper accepts structured fields,
never a command line, and invokes programs with fixed argument vectors.

SSID values are untrusted byte strings. The implementation must safely handle
spaces, quotes, backslashes, control characters, Unicode, and the maximum 32
octets without interpolation into shell or configuration syntax. Passphrases
must meet the selected security mode's rules and must never appear in metrics,
journald, URLs, crash reports, backups, or the owner-facing status API.

Use CSRF tokens, strict Host checking, request-size limits, attempt throttling,
and a 15-minute absolute lifetime. A successful connection immediately stops
the AP. Loss of Internet later does not automatically reopen it; only the
protected physical gesture or local console can do so.

## Recovery gesture

Reserve a purpose-built owner token plus a deliberate handset/keypad sequence,
for example: swipe the owner token, lift the handset, and hold `0` for five
seconds. The display asks for confirmation before creating the setup network.
This avoids accidental activation and prevents an unauthenticated passerby
from reopening onboarding with a guessable key sequence.

The old network remains available for rollback until the replacement has
passed all connectivity checks. A local-console command can cancel setup or
restore the last-known-good configuration even if the radio is unusable.

## Deployment phases

1. Build and unit-test the parser, state machine, atomic candidate/rollback
   helper, portal, and nftables setup-mode rules.
2. Test on a spare Pi or with local serial/keyboard recovery. Switching a
   single Wi-Fi radio from station to AP mode will intentionally sever SSH.
3. Exercise iOS, Android, macOS, and Windows captive-portal behavior plus
   hidden, open, WPA2, and WPA2/WPA3-transition networks.
4. Add `hostapd` and `dnsmasq` to the factory image and recovery media. Do not
   make an OTA depend on installing packages from the Internet.
5. Enable it on the production phone only during a local maintenance session,
   then prove remote maintenance returns after reprovisioning.

## Acceptance evidence

- Two uncoached owners complete first-time setup from a phone.
- Wrong credentials and power loss restore the AP and last-known-good network.
- A setup client cannot reach SSH, the daemon admin API, the LAN, or secrets.
- Captured logs, metrics, backups, and HTTP history contain no SSID passphrase.
- The AP never appears during ordinary WAN, DNS, update-server, or maintenance
  outages.
