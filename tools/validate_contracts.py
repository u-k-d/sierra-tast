import json
import re
from pathlib import Path

from hash_semantics import contract_sha256, doc_sha256

ROOT = Path(__file__).resolve().parents[1]
LOCK = json.loads((ROOT / "layers.lock.json").read_text(encoding="utf-8"))
META_SCHEMA = json.loads((ROOT / "meta.contract.schema.json").read_text(encoding="utf-8"))

allowed = {x["from"]: x for x in LOCK["allowed_deps"]}
required_kinds = {"api_signatures", "file_layout", "module_boundaries", "plan_ast_shape", "diagnostics"}

manifest_paths = sorted((ROOT / "contracts").glob("L*_*.manifest.json"))
if len(manifest_paths) != 9:
    raise SystemExit(f"expected 9 manifests, found {len(manifest_paths)}")

try:
    import jsonschema
except Exception:
    jsonschema = None


def extract_doc_contract(doc_path: Path):
    text = doc_path.read_text(encoding="utf-8")
    m = re.search(r"```json\n(.*?)\n```", text, re.DOTALL)
    if not m:
        raise SystemExit(f"{doc_path}: missing JSON contract block")
    try:
        return json.loads(m.group(1))
    except json.JSONDecodeError as ex:
        raise SystemExit(f"{doc_path}: invalid JSON contract block: {ex}") from ex


def validate_required_from_schema(instance, schema, path="$"):
    if not isinstance(schema, dict):
        return
    required = schema.get("required", [])
    if isinstance(instance, dict):
        for key in required:
            if key not in instance:
                raise SystemExit(f"{path}: missing required key '{key}'")

    props = schema.get("properties")
    if isinstance(props, dict) and isinstance(instance, dict):
        for k, v in props.items():
            if k in instance:
                validate_required_from_schema(instance[k], v, f"{path}.{k}")

    if "items" in schema and isinstance(instance, list):
        for i, item in enumerate(instance):
            validate_required_from_schema(item, schema["items"], f"{path}[{i}]")


for mp in manifest_paths:
    m = json.loads(mp.read_text(encoding="utf-8"))
    lid = m["layer_id"]
    dep = allowed[lid]
    stem = mp.stem.replace(".manifest", "")
    doc_path = ROOT / "docs" / "normative" / f"{stem}.md"

    if not doc_path.exists():
        raise SystemExit(f"{lid}: missing normative doc {doc_path}")

    doc_contract = extract_doc_contract(doc_path)
    if doc_contract != m:
        raise SystemExit(f"{lid}: manifest drift from normative doc contract block")

    # Validate required keys from meta schema always (works without external dependencies).
    validate_required_from_schema(m, META_SCHEMA)

    # Optional full JSON Schema validation when jsonschema is available.
    if jsonschema is not None:
        try:
            jsonschema.validate(instance=m, schema=META_SCHEMA)
        except Exception as ex:
            raise SystemExit(f"{lid}: meta-schema validation failed: {ex}") from ex

    # Validate distinct hash semantics.
    hashes = m.get("hashes")
    if not isinstance(hashes, dict) or "doc_sha256" not in hashes or "contract_sha256" not in hashes:
        raise SystemExit(f"{lid}: missing hash fields")
    expected_contract_hash = contract_sha256(m)
    expected_doc_hash = doc_sha256(doc_path.read_text(encoding="utf-8"))
    if hashes["contract_sha256"] != expected_contract_hash:
        raise SystemExit(f"{lid}: contract_sha256 mismatch with canonical contract payload")
    if hashes["doc_sha256"] != expected_doc_hash:
        raise SystemExit(f"{lid}: doc_sha256 mismatch with normalized doc content")

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

    diagnostics_groups = [g for g in m["normative_ast_checks"]["check_groups"] if g["kind"] == "diagnostics"]
    if len(diagnostics_groups) != 1:
        raise SystemExit(f"{lid}: expected exactly one diagnostics check group")
    diagnostics_checks = diagnostics_groups[0]["checks"]
    diag_by_fixture = {
        c["assert"]["params"].get("fixture_id")
        for c in diagnostics_checks
        if c.get("assert", {}).get("kind") == "invalid_fixture_yields_error"
    }
    for fx in m["fixtures"]["invalid"]:
        if fx["id"] not in diag_by_fixture:
            raise SystemExit(f"{lid}: missing diagnostics check for invalid fixture {fx['id']}")

    # Validate compile output artifacts are present.
    for artifact in m["compile_outputs"]["artifacts"]:
        ap = ROOT / artifact["path"]
        if not ap.exists():
            raise SystemExit(f"{lid}: missing compile artifact path {artifact['path']}")

    for fx in m["fixtures"]["valid"] + m["fixtures"]["invalid"]:
        fp = ROOT / fx["path"]
        if not fp.exists():
            raise SystemExit(f"{lid}: missing fixture file {fx['path']}")

print("contract validation passed")
