import json
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CLI = ROOT / "build" / "sre_planlint.exe"
L4_MANIFEST = ROOT / "contracts" / "L4_rule_chain_dsl.manifest.json"
VALID_FIXTURE = ROOT / "fixtures" / "L4_rule_chain_dsl" / "valid" / "fix_valid_01.json"


def read_jsonl(path: Path) -> list[dict]:
    if not path.exists():
        return []
    return [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines() if line.strip()]


def main() -> int:
    if not CLI.exists():
        raise SystemExit(f"missing binary: {CLI}")

    original = L4_MANIFEST.read_text(encoding="utf-8")
    try:
        narrowed = json.loads(original)
        owned = ["/chains", "/signals", "/gates"]
        narrowed["schema_scope"]["owned_pointers"] = owned
        for node in narrowed.get("ast_nodes", []):
            for inv in node.get("invariants", []):
                rule = inv.get("rule", {})
                if rule.get("kind") == "no_cross_layer_pointer_access":
                    rule.setdefault("params", {})["owned_pointers"] = owned
        for group in narrowed.get("normative_ast_checks", {}).get("check_groups", []):
            if group.get("kind") != "plan_ast_shape":
                continue
            for check in group.get("checks", []):
                assertion = check.get("assert", {})
                if assertion.get("kind") == "cnf_pointer_in_scope":
                    assertion.setdefault("params", {})["pointers"] = owned
        L4_MANIFEST.write_text(json.dumps(narrowed, indent=2, ensure_ascii=True) + "\n", encoding="utf-8")

        with tempfile.TemporaryDirectory(prefix="sre_scope_") as td:
            tmp = Path(td)
            jsonl = tmp / "diag.jsonl"
            txt = tmp / "diag.txt"
            proc = subprocess.run(
                [str(CLI), str(VALID_FIXTURE), str(jsonl), str(txt)],
                cwd=ROOT,
                capture_output=True,
                text=True,
            )
            diags = read_jsonl(jsonl)
            if proc.returncode == 0:
                print("scope check expected failure but plan passed")
                return 2
            has_scope_error = any(
                d.get("code") == "E_PLAN_POINTER_OUT_OF_SCOPE" and d.get("layer_id") == "rule_chain_dsl"
                for d in diags
            )
            if not has_scope_error:
                print("missing E_PLAN_POINTER_OUT_OF_SCOPE for narrowed L4 scope")
                return 2
    finally:
        L4_MANIFEST.write_text(original, encoding="utf-8")

    print("scope enforcement checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
