import json
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CLI = ROOT / "build" / "sre_planlint.exe"
BASE_FIXTURE = ROOT / "fixtures" / "L0_core_identity_io" / "valid" / "fix_valid_01.json"


def run_plan(plan: dict, name: str) -> tuple[int, list[dict]]:
    with tempfile.TemporaryDirectory(prefix=f"sre_go_{name}_") as td:
        tdp = Path(td)
        plan_path = tdp / "plan.json"
        jsonl_path = tdp / "diag.jsonl"
        txt_path = tdp / "diag.txt"
        plan_path.write_text(json.dumps(plan, indent=2) + "\n", encoding="utf-8")
        proc = subprocess.run(
            [str(CLI), str(plan_path), str(jsonl_path), str(txt_path)],
            cwd=ROOT,
            capture_output=True,
            text=True,
        )
        diags = []
        if jsonl_path.exists():
            for line in jsonl_path.read_text(encoding="utf-8").splitlines():
                if line.strip():
                    diags.append(json.loads(line))
        return proc.returncode, diags


def has_check(diags: list[dict], check_id: str, severity: str | None = None) -> bool:
    for d in diags:
        if d.get("check_id") != check_id:
            continue
        if severity is not None and d.get("severity") != severity:
            continue
        return True
    return False


def main() -> int:
    if not CLI.exists():
        raise SystemExit(f"missing binary: {CLI}")

    base = json.loads(BASE_FIXTURE.read_text(encoding="utf-8"))
    failures: list[str] = []

    c1 = json.loads(json.dumps(base))
    c1.setdefault("execution", {})["permissions"] = {"allow_filesystem_write": False}
    rc, diags = run_plan(c1, "output_permission_denied")
    if rc == 0 or not has_check(diags, "CHK_OUTPUT_FILESYSTEM_PERMISSION"):
        failures.append("output_permission_denied should fail with CHK_OUTPUT_FILESYSTEM_PERMISSION")

    c2 = json.loads(json.dumps(base))
    c2.setdefault("execution", {})["permissions"] = {"allow_filesystem_write": True}
    rc, diags = run_plan(c2, "output_permission_allowed")
    if rc != 0:
        failures.append("output_permission_allowed should pass")

    c3 = json.loads(json.dumps(base))
    c3.setdefault("meta", {})["engine_capabilities_required"] = ["cap:sre:nonexistent:v1"]
    rc, diags = run_plan(c3, "required_cap_unsupported")
    if rc == 0 or not has_check(diags, "CHK_REQUIRED_CAP_SUPPORTED"):
        failures.append("required_cap_unsupported should fail with CHK_REQUIRED_CAP_SUPPORTED")

    c4 = json.loads(json.dumps(base))
    c4.setdefault("meta", {})["engine_capabilities_optional"] = ["cap:sre:optional_unknown:v1"]
    rc, diags = run_plan(c4, "optional_cap_unsupported")
    if rc != 0 or not has_check(diags, "CHK_OPTIONAL_CAP_UNSUPPORTED", "warn"):
        failures.append("optional_cap_unsupported should pass with warn CHK_OPTIONAL_CAP_UNSUPPORTED")

    c5 = json.loads(json.dumps(base))
    c5["dag"] = {
        "nodes": [
            {"id": "n1", "parents": ["n3"]},
            {"id": "n2", "parents": []},
        ]
    }
    rc, diags = run_plan(c5, "dag_parent_missing")
    if rc == 0 or not has_check(diags, "CHK_DAG_NODE_PARENT_EXISTS"):
        failures.append("dag_parent_missing should fail with CHK_DAG_NODE_PARENT_EXISTS")

    if failures:
        for f in failures:
            print(f)
        return 2

    print("governance/output checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
