# Per-device as-built records

Copy `TEMPLATE.md` once for every physical phone. Do not mark a record accepted
while any `REQUIRED` value remains. Hash the exact fabrication exports and HEX
files installed—not merely nearby source files. Store assembly photographs in
the device's adjacent `photos/` directory and list their hashes.

Power-loss testing must interrupt boot, idle, an active call, content-state
save, OTA download, MCU flash, and host activation. Brownout testing records
the supply, load, minimum voltage, duration, observed reset causes, and safe
recovery result. A narrative note without measurements is not evidence.

Generate a machine-readable starting record on the actual phone, then fill it
with observations and exact installed-artifact identities:

```bash
python3 tools/as_built_record.py capture --device-id phone-001 \
  --operator OPERATOR --output hardware/as-built/phone-001/evidence.json
python3 tools/as_built_record.py validate \
  hardware/as-built/phone-001/evidence.json
```

`validate` intentionally fails while any physical observation, measurement,
photo, installed identity, or recovery drill is absent. Candidate repository
hashes emitted by `capture` are never represented as installed artifacts.
