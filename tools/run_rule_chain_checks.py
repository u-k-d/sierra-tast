import json
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CLI = ROOT / "build" / "sre_planlint.exe"
BASE_FIXTURE = ROOT / "fixtures" / "L0_core_identity_io" / "valid" / "fix_valid_01.json"


def run_plan(plan: dict, name: str) -> tuple[int, list[dict]]:
    with tempfile.TemporaryDirectory(prefix=f"sre_chain_{name}_") as td:
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


def has_check(diags: list[dict], check_id: str) -> bool:
    return any(d.get("check_id") == check_id for d in diags)


def main() -> int:
    if not CLI.exists():
        raise SystemExit(f"missing binary: {CLI}")

    base = json.loads(BASE_FIXTURE.read_text(encoding="utf-8"))
    failures: list[str] = []

    # Missing gate reference should fail when require_gates_exist=true.
    c1 = json.loads(json.dumps(base))
    c1.setdefault("validation", {})["require_gates_exist"] = True
    c1["chains"]["main"]["steps"][0]["when"] = "@gate.missing_gate"
    rc, diags = run_plan(c1, "missing_gate")
    if rc == 0 or not has_check(diags, "CHK_CHAIN_GATE_EXISTS"):
        failures.append("missing_gate should fail with CHK_CHAIN_GATE_EXISTS")

    # Forward step dependency should fail.
    c2 = json.loads(json.dumps(base))
    c2["chains"]["main"]["steps"] = [
        {"kind": "emit", "from": "@step.main.second", "as": "first"},
        {"kind": "emit", "from": "@study.ema_fast.value", "as": "second"},
    ]
    rc, diags = run_plan(c2, "step_dep_order")
    if rc == 0 or not has_check(diags, "CHK_CHAIN_STEP_DEP_ORDER"):
        failures.append("step_dep_order should fail with CHK_CHAIN_STEP_DEP_ORDER")

    # Untyped step input should fail when typed refs preferred.
    c3 = json.loads(json.dumps(base))
    c3.setdefault("validation", {})["prefer_typed_step_io_refs"] = True
    c3["chains"]["main"]["steps"] = [{"kind": "emit", "from": "ema_fast.value", "as": "ema"}]
    rc, diags = run_plan(c3, "typed_ref")
    if rc == 0 or not has_check(diags, "CHK_CHAIN_TYPED_REF"):
        failures.append("typed_ref should fail with CHK_CHAIN_TYPED_REF")

    # Valid gate reference path should pass.
    c4 = json.loads(json.dumps(base))
    c4["gates"] = {"ready": {"expr": "1"}}
    c4["chains"]["main"]["steps"][0]["when"] = "@gate.ready"
    rc, diags = run_plan(c4, "valid_gate")
    if rc != 0:
      failures.append("valid_gate should pass")

    if failures:
        for f in failures:
            print(f)
        return 2

    print("rule-chain checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

