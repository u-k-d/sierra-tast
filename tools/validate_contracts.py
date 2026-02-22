import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LOCK = json.loads((ROOT / "layers.lock.json").read_text(encoding="utf-8"))

allowed = {x["from"]: x for x in LOCK["allowed_deps"]}
required_kinds = {"api_signatures", "file_layout", "module_boundaries", "plan_ast_shape", "diagnostics"}

manifest_paths = sorted((ROOT / "contracts").glob("L*_*.manifest.json"))
if len(manifest_paths) != 9:
    raise SystemExit(f"expected 9 manifests, found {len(manifest_paths)}")

for mp in manifest_paths:
    m = json.loads(mp.read_text(encoding="utf-8"))
    lid = m["layer_id"]
    dep = allowed[lid]

    declared_layers = set(m["dependencies"]["allowed_layer_ids"])
    if not declared_layers.issubset(set(dep["to_layers"])):
        raise SystemExit(f"{lid}: declared layer deps not subset of layers.lock")

    declared_interfaces = {x["interface_id"] for x in m["dependencies"]["allowed_interfaces"]}
    if not declared_interfaces.issubset(set(dep["to_interfaces"])):
        raise SystemExit(f"{lid}: declared interface deps not subset of layers.lock")

    kinds = {g["kind"] for g in m["normative_ast_checks"]["check_groups"]}
    missing = required_kinds - kinds
    if missing:
        raise SystemExit(f"{lid}: missing check groups {sorted(missing)}")

    vmin = m["fixtures"]["minimums"]["valid"]
    imin = m["fixtures"]["minimums"]["invalid"]
    if len(m["fixtures"]["valid"]) < vmin:
        raise SystemExit(f"{lid}: valid fixture count below minimum")
    if len(m["fixtures"]["invalid"]) < imin:
        raise SystemExit(f"{lid}: invalid fixture count below minimum")

    for fx in m["fixtures"]["valid"] + m["fixtures"]["invalid"]:
        fp = ROOT / fx["path"]
        if not fp.exists():
            raise SystemExit(f"{lid}: missing fixture file {fx['path']}")

print("contract validation passed")
