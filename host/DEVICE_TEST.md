# Device Testing

Run integration tests against the real Millennium payphone when it's connected and the daemon is running.

## Remote Device

- **Host:** 192.168.86.145
- **User:** matzen

## Prerequisites

- Daemon running on the device (`sudo systemctl status daemon.service`)
- Device reachable on the network (port 8081 for HTTP API)

## Run API Tests (from your machine)

```bash
make api-test HOST=192.168.86.145
```

Or set the variable once:

```bash
export HOST=192.168.86.145
make api-test
```

## Run API Tests (from the device)

SSH into the device and run locally:

```bash
ssh matzen@192.168.86.145 'cd millennium/host && make api-test'
```

## Troubleshooting

If tests fail with "Cannot reach":

1. Check daemon is running: `ssh matzen@192.168.86.145 'sudo systemctl status daemon.service'`
2. Verify port 8081 is listening: `ssh matzen@192.168.86.145 'ss -tlnp | grep 8081'`
3. Confirm network connectivity: `curl -s http://192.168.86.145:8081/api/health`
