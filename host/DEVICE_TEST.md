# Device Testing

Run integration tests against the real Millennium payphone when it's connected
and the daemon is running.

## Finding the device

The Pi's address is **DHCP and has changed before** (e.g. `.145` → `.115` →
`.152`), so don't rely on a fixed IP. Prefer the mDNS hostname; fall back to the
router's DHCP table:

```bash
PI=raspberrypi.local            # mDNS hostname (can be flaky — fall back to an IP)
ping -c2 "$PI"                  # confirm it resolves and is up
# If it doesn't resolve, find the current IP on the LAN:
arp -a | grep -i 'b8:27\|dc:a6\|e4:5f\|d8:3a'   # Raspberry Pi OUIs
```

The daemon serves the HTTP API on **port 80**.

## Prerequisites

- Daemon running on the device (`ssh matzen@$PI 'sudo systemctl status daemon.service'`)
- Device reachable on the network (port 80 for the HTTP API)

## Run API Tests (from your machine)

```bash
make api-test HOST=raspberrypi.local      # or HOST=<pi-ip>
```

Or set the variable once:

```bash
export HOST=raspberrypi.local
make api-test
```

> `make device-test` is a shortcut that hardwires the old `192.168.86.145`
> address — only use it if the Pi still happens to live there. Prefer
> `make api-test HOST=...` with the current address.

## Run API Tests (from the device)

SSH into the device and run locally (no networking flakiness):

```bash
ssh matzen@raspberrypi.local 'cd millennium/host && make api-test'
```

## Troubleshooting

If tests fail with "Cannot reach":

1. Check the daemon is running: `ssh matzen@$PI 'sudo systemctl status daemon.service'`
2. Verify port 80 is listening: `ssh matzen@$PI 'ss -tlnp | grep ":80 "'`
3. Confirm network connectivity: `curl -s http://$PI/api/health`
4. If `raspberrypi.local` doesn't resolve, the Pi may be powered off or on a new
   IP — find it via the router's DHCP table or the `arp -a` OUI grep above.
