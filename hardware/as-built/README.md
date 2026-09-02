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

Fill the record through the CLI so file hashes and measurement requirements
cannot be omitted accidentally:

```bash
python3 tools/as_built_record.py set-physical evidence.json \
  --field pcb_revision --value phonev6
python3 tools/as_built_record.py add-photo evidence.json \
  --path photos/interior.jpg --description "Interior, PCB marking and wiring"
python3 tools/as_built_record.py record-installed evidence.json \
  --name host_binary --path /opt/millennium/current/host/millennium-daemon \
  --identity "sequence 9, version 0.4.0"
python3 tools/as_built_record.py record-test evidence.json \
  --name controlled_brownout --result pass --date 2026-09-02 \
  --instrument-and-load "bench supply and complete installed phone" \
  --minimum-voltage 4.72 --evidence-file evidence/brownout.txt
```

Use `record-installed` for every artifact named by `--help`, `set-physical`
for every physical field, and `record-test` for every interruption scenario.
A failed observation is recorded with `--result fail` and remains an open gate.

`validate` intentionally fails while any physical observation, measurement,
photo, installed identity, or recovery drill is absent. Candidate repository
hashes emitted by `capture` are never represented as installed artifacts.

## Final handoff gate

After the as-built record and first-time-caller playtest record are complete,
create one final record that also binds the four real Wi-Fi client tests, two
physically distinct encrypted signing-key copies, and an interactive
maintenance session performed from outside the home network:

```bash
python3 tools/handoff_acceptance.py template --device-id phone-001 \
  --as-built-record evidence.json \
  --playtest-record playtest-last-line-2.1.0.json \
  --output hardware/as-built/phone-001/handoff.json

python3 tools/handoff_acceptance.py record-wifi \
  hardware/as-built/phone-001/handoff.json --platform ios --result pass \
  --date 2026-09-02 --evidence-file evidence/wifi-ios.txt

python3 tools/handoff_acceptance.py add-key-copy \
  hardware/as-built/phone-001/handoff.json --media-label "USB-A" \
  --ciphertext /Volumes/USB-A/release-2026-08.pem.aes256 \
  --verified-at 2026-09-02 --recovery-tested-at 2026-09-02 \
  --copy-evidence-file evidence/key-copy-usb-a.json \
  --recovery-evidence-file evidence/key-recovery-usb-a.json

python3 tools/handoff_acceptance.py record-external \
  hardware/as-built/phone-001/handoff.json --result pass --date 2026-09-02 \
  --network-description "cellular hotspot, not home ISP" \
  --evidence-file evidence/external-maintenance.txt

python3 tools/handoff_acceptance.py validate \
  hardware/as-built/phone-001/handoff.json
```

Repeat `record-wifi` for `android`, `macos`, and `windows`, and `add-key-copy`
for a second removable device. The tool hashes each ciphertext itself, rejects
duplicate media labels, hashes and rechecks every referenced evidence file,
validates the linked as-built and playtest records, and will not mark the phone
acceptable from QEMU or unsupported narrative assertions alone.
