# Sierra Research Engine Spec Bundle

This repository contains a contract-driven validation pipeline for Sierra study plans.

## What is in `plans/`
- `plans/example_plan.v2_6_2.json`: example plan input for the v2.6.2 contract.

## How to run a plan check
Use the plan linter executable and write diagnostics into the `artifacts/` folder:

```powershell
build\sre_planlint.exe plans\example_plan.v2_6_2.json artifacts\diagnostics.ast_checks.jsonl artifacts\diagnostics.ast_checks.txt
```

Behavior:
- Exit code `0`: plan passed all enforced manifest checks.
- Exit code `1`: at least one contract/semantic check failed.

## Diagnostics outputs
- `artifacts/diagnostics.ast_checks.jsonl`: machine-readable diagnostics (one JSON object per line).
- `artifacts/diagnostics.ast_checks.txt`: human-readable grouped summary.

Each diagnostic includes:
- `code`: stable error code (for example `E_SEM_STALE_WORKER_DATA`).
- `layer_id`: owning layer (`L0`-`L8` manifest owner).
- `check_id`: manifest check identifier that failed.
- `blame_pointers`: JSON pointers to the failing plan fields.
- `severity`: `error` blocks execution; `warn` is informational.

## Runtime behavior
`sre_planlint` now supports additive opt-in augmentation paths:
- `outputs.dataset.source="layer2_event_emitter"` runs `execution.layer2` indicator DAG evaluation and event emission.
- Layer2 DAG invariants emit stable errors: `E_L2_DAG_CYCLE`, `E_L2_MISSING_NODE_DEP`, `E_L2_UNKNOWN_NODE_KIND`.
- When Layer3 outcomes/bucketing/metrics blocks are configured, runtime emits `outcomes_per_event` and `bucket_stats`.
- Same-bar leakage in Layer3 outcomes emits `E_L3_OUTCOME_LEAKAGE_SAME_BAR`.

Legacy behavior remains unchanged by default:
- If `execution.layer2` is absent, legacy dataset emission behavior is used.
- If `outputs.dataset.source` is absent, it defaults to `bars`.
- `outputs.layer3` with only legacy `rr_menu` settings remains on the existing not-implemented runtime path (`E_NOT_IMPLEMENTED`).

## Optional contract checks
Run supporting checks used in this repo:

```powershell
python tools/validate_contracts.py
python tools/run_fixture_runtime_checks.py
python tools/run_semantic_tripwire_checks.py
python tools/run_scope_enforcement_checks.py
```

These ensure the plan/runtime behavior stays aligned with layer manifests, schema contracts, and diagnostics taxonomy.
