import json
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CLI = ROOT / "build" / "sre_planlint.exe"
BASE_FIXTURE = ROOT / "fixtures" / "L0_core_identity_io" / "valid" / "fix_valid_01.json"


def run_plan(plan: dict, name: str) -> tuple[int, list[dict]]:
    with tempfile.TemporaryDirectory(prefix=f"sre_perm_{name}_") as td:
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
    base.setdefault("parameters", {})
    base["parameters"]["permute"] = [
        {"id": "p_len", "kind": "study_input", "study": "ema_fast", "input": "length", "values": [8, 13]}
    ]

    failures: list[str] = []

    # Policy disabled + on_violation error => failure with specific check.
    c1 = json.loads(json.dumps(base))
    c1.setdefault("validation", {})["sierra_study_input_permute_policy"] = "disabled"
    c1["validation"]["sierra_study_input_permute_on_violation"] = "error"
    rc, diags = run_plan(c1, "disabled_error")
    if rc == 0 or not has_check(diags, "CHK_PERMUTE_POLICY_DISABLED"):
        failures.append("disabled_error policy should fail with CHK_PERMUTE_POLICY_DISABLED")

    # Managed only + bind mode => failure.
    c2 = json.loads(json.dumps(base))
    c2.setdefault("validation", {})["sierra_study_input_permute_policy"] = "managed_only"
    c2["studies"]["ema_fast"]["mode"] = "bind"
    rc, diags = run_plan(c2, "managed_only_bind")
    if rc == 0 or not has_check(diags, "CHK_PERMUTE_POLICY_MANAGED_ONLY"):
        failures.append("managed_only_bind should fail with CHK_PERMUTE_POLICY_MANAGED_ONLY")

    # Allowlist bind mode with missing study allowlist => failure.
    c3 = json.loads(json.dumps(base))
    c3.setdefault("validation", {})["sierra_study_input_permute_policy"] = "allowlist_bind_mode"
    c3["studies"]["ema_fast"]["mode"] = "bind"
    c3["validation"]["sierra_bind_mode_allowlist"] = []
    rc, diags = run_plan(c3, "allowlist_missing")
    if rc == 0 or not has_check(diags, "CHK_PERMUTE_BIND_ALLOWLIST_STUDY"):
        failures.append("allowlist_missing should fail with CHK_PERMUTE_BIND_ALLOWLIST_STUDY")

    # Warn mode should pass with warning.
    c4 = json.loads(json.dumps(base))
    c4.setdefault("validation", {})["sierra_study_input_permute_policy"] = "managed_only"
    c4["validation"]["sierra_study_input_permute_on_violation"] = "warn"
    c4["studies"]["ema_fast"]["mode"] = "bind"
    rc, diags = run_plan(c4, "warn_mode")
    if rc != 0 or not has_check(diags, "CHK_PERMUTE_POLICY_MANAGED_ONLY", "warn"):
        failures.append("warn_mode should pass with warning CHK_PERMUTE_POLICY_MANAGED_ONLY")

    # Skip mode should pass with warning.
    c5 = json.loads(json.dumps(base))
    c5.setdefault("validation", {})["sierra_study_input_permute_policy"] = "disabled"
    c5["validation"]["sierra_study_input_permute_on_violation"] = "skip_permutation"
    rc, diags = run_plan(c5, "skip_mode")
    if rc != 0 or not has_check(diags, "CHK_PERMUTE_POLICY_DISABLED", "warn"):
        failures.append("skip_mode should pass with warning CHK_PERMUTE_POLICY_DISABLED")

    if failures:
        for f in failures:
            print(f)
        return 2

    print("permute policy checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

