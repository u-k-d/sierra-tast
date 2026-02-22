import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LAYER_TAG = "L2_sierra_runtime_topology"


def test_contract_groups_and_fixture_counts():
    manifest = json.loads((ROOT / "contracts" / f"{LAYER_TAG}.manifest.json").read_text(encoding="utf-8"))
    kinds = {g["kind"] for g in manifest["normative_ast_checks"]["check_groups"]}
    required = ['api_signatures', 'file_layout', 'module_boundaries', 'plan_ast_shape', 'diagnostics']
    missing = [k for k in required if k not in kinds]
    assert not missing, f"missing check kinds: {missing}"

    vmin = manifest["fixtures"]["minimums"]["valid"]
    imin = manifest["fixtures"]["minimums"]["invalid"]
    assert len(manifest["fixtures"]["valid"]) >= vmin
    assert len(manifest["fixtures"]["invalid"]) >= imin
