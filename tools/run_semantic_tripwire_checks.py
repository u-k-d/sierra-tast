import json
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CLI = ROOT / "build" / "sre_planlint.exe"
VALID_FIXTURE = ROOT / "fixtures" / "L0_core_identity_io" / "valid" / "fix_valid_01.json"


def run(plan: dict, name: str) -> tuple[int, list[dict]]:
    with tempfile.TemporaryDirectory(prefix="sre_sem_") as td:
        tdp = Path(td)
        plan_path = tdp / f"{name}.json"
        jsonl = tdp / f"{name}.jsonl"
        txt = tdp / f"{name}.txt"
        plan_path.write_text(json.dumps(plan, indent=2) + "\n", encoding="utf-8")
        proc = subprocess.run(
            [str(CLI), str(plan_path), str(jsonl), str(txt)],
            cwd=ROOT,
            capture_output=True,
            text=True,
        )
        diags = []
        if jsonl.exists():
            for line in jsonl.read_text(encoding="utf-8").splitlines():
                if line.strip():
                    diags.append(json.loads(line))
        return proc.returncode, diags


def has_sem(diags: list[dict], check_id: str, code: str) -> bool:
    return any(d.get("code") == code and d.get("check_id") == check_id for d in diags)


def main() -> int:
    if not CLI.exists():
        raise SystemExit(f"missing CLI binary: {CLI}")
    base = json.loads(VALID_FIXTURE.read_text(encoding="utf-8"))

    cases = []

    c1 = json.loads(json.dumps(base))
    c1["chains"]["main"]["steps"][0]["from"] = "@bar.current.same_bar"
    cases.append(("same_bar_leakage", c1, "CHK_SEM_SAME_BAR_LEAKAGE", "E_SEM_SAME_BAR_LEAKAGE"))

    c2 = json.loads(json.dumps(base))
    c2.setdefault("validation", {})["require_sierra_readiness_contract"] = True
    c2.setdefault("execution", {})["sentinel"] = {"enabled": True, "ready_value": 1.0, "study_id": 1, "subgraph": 0}
    cases.append(("sentinel_staleness", c2, "CHK_SEM_STALENESS_SENTINEL_CONSTANT", "E_SEM_STALE_WORKER_DATA"))

    c3 = json.loads(json.dumps(base))
    c3.setdefault("validation", {})["sierra_study_input_permute_policy"] = "managed_only"
    c3["studies"]["ema_fast"]["mode"] = "bind"
    c3.setdefault("parameters", {})["permute"] = [
        {"id": "p1", "kind": "study_input", "study": "ema_fast", "input": "len", "values": [10, 20]}
    ]
    cases.append(("permute_bind_drift", c3, "CHK_SEM_PERMUTE_BIND_MODE_DRIFT", "E_SEM_PERMUTE_BIND_MODE_DRIFT"))

    c4 = json.loads(json.dumps(base))
    c4["chains"]["main"]["steps"][0]["when"] = "@gate.missing_gate"
    cases.append(("missing_gate_ref", c4, "CHK_SEM_GATE_EXISTS", "E_SEM_GATE_REFERENCE_MISSING"))

    c5 = json.loads(json.dumps(base))
    c5["outputs"]["dataset"]["fields"] = [{"id": "dedupe_key", "from": "@step.main.id"}]
    cases.append(("dedupe_session_boundary", c5, "CHK_SEM_DEDUPE_SESSION_BOUNDARY", "E_SEM_DEDUPE_SESSION_BOUNDARY_MISSING"))

    failures: list[str] = []
    for name, plan, expected_check, expected_code in cases:
        rc, diags = run(plan, name)
        if rc == 0:
            failures.append(f"{name}: expected semantic failure but rc=0")
            continue
        if not has_sem(diags, expected_check, expected_code):
            failures.append(f"{name}: missing expected semantic diagnostic {expected_code}/{expected_check}")

    if failures:
        for f in failures:
            print(f)
        return 2
    print("semantic tripwire checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
