# Executable acceptance coverage

`coverage.json` maps each shipped function group to executable automated suites,
test fidelity, applicable fault categories, and narrowly scoped physical or
human gates. `inventory.json` is generated from the actual production surface:
installed executables, systemd units, HTTP routes, CLI actions, updater states,
Wi-Fi routes, maintenance actions, plugins, story events, MCU messages, and
recovery tools.

Validate both files and inspect every item with:

```bash
python3 tools/generate_coverage_inventory.py --check
python3 tools/coverage_ledger.py validate
python3 tools/coverage_ledger.py inventory
python3 tools/coverage_ledger.py physical-plan
```

When a production surface changes, regenerate `inventory.json`, map any new
function group in `coverage.json`, and add the strongest feasible automated
suite. CI rejects stale inventory, missing source files, missing suite commands,
unowned items, unknown gates, omitted fidelity/fault profiles, and physical
claims without an explicit mock limitation and linked procedure.

Run local suites with `tools/coverage_ledger.py run-automated --suite host
--suite contracts`. Run the `qemu` suite on `anima`; QEMU evidence never
substitutes for an item marked `physical-only`.

The complete release procedure and regression rule are in
`docs/TESTING_STRATEGY.md`. `tools/release_gate.py` is the final automated
verdict: it rejects stale commits, incomplete QEMU runs, generic-guest-only
evidence, exact-image failures, failed local suites, and false physical claims.
