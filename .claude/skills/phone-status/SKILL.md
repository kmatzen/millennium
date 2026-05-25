---
name: phone-status
description: Quick health check of the live Millennium phone — daemon service state, SIP registration, phone state/coin balance, and recent errors over SSH. Use when asked how the phone/daemon is doing, whether it's registered, or to triage a problem before deeper debugging.
---

# Check the phone's health

A fast triage of the running phone: is the daemon up, is it registered with the
SIP provider, what state is it in, and any recent errors.

## Connection

```bash
PI=matzen@raspberrypi.local       # DHCP IP has changed before (.145 -> .115); prefer the hostname
SSH="ssh -i $HOME/.ssh/id_ed25519_claude_anima -o IdentitiesOnly=yes -o BatchMode=yes -o ConnectTimeout=8"
```

If SSH times out, the Pi may be **powered off** or have a new IP — confirm with
`ping -c2 raspberrypi.local`, else find it via the router's DHCP table or
`arp -a | grep -i 'b8:27\|dc:a6\|e4:5f'`.

## Check

```bash
$SSH $PI '
  echo "== service =="; systemctl is-active daemon.service; \
  echo "NRestarts=$(systemctl show daemon.service -p NRestarts --value)"; \
  echo "uptime: $(systemctl show daemon.service -p ActiveEnterTimestamp --value)"; \
  echo "== state =="; curl -s --max-time 4 http://127.0.0.1:8081/api/state; echo; \
  echo "== recent errors =="; \
  sudo journalctl -u daemon.service --since "10 min ago" --no-pager \
    | grep -iE "error|fail|warn|segf|core|underrun" | tail -15'
```

## Reading it
- `active` + `NRestarts=0` → daemon healthy and not crash-looping. A climbing
  `NRestarts` means it's crashing — pull a backtrace from the coredump / run
  under `gdb`.
- `/api/state` JSON: `current_state` (1=IDLE_DOWN, 2=IDLE_UP, 3=CALL_INCOMING,
  4=CALL_ACTIVE), `sip_registered` (want `1`), `sip_last_error` (want empty),
  `inserted_cents` (coin balance), `keypad_buffer`, `line1`/`line2` (VFD).
- `sip_registered:0` → check `sip.*` in `/etc/millennium/daemon.conf` and the
  TLS/registrar reachability in the journal.

## Related
- Metrics (Prometheus/JSON): `curl -s http://<pi>:8080/api/metrics`.
- Live event stream: `$SSH $PI 'sudo journalctl -u daemon.service -f'`, then
  exercise the hook/keypad/coins.
- To redeploy after a fix, use the `deploy-daemon` skill.
