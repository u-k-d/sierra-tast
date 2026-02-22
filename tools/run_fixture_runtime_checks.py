import json
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CLI = ROOT / "build" / "sre_planlint.exe"
CONTRACTS = sorted((ROOT / "contracts").glob("L*_*.manifest.json"))


def run_cli(plan_path: Path, jsonl: Path, summary: Path) -> int:
    proc = subprocess.run(
        [str(CLI), str(plan_path), str(jsonl), str(summary)],
        cwd=ROOT,
        capture_output=True,
        text=True,
    )
    return proc.returncode


def read_jsonl(path: Path) -> list[dict]:
    if not path.exists():
        return []
    rows = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        rows.append(json.loads(line))
    return rows


def first_primary_error(manifest: dict) -> str:
    for ec in manifest["public_api"]["error_codes"]:
        code = ec["code"]
        if code != "E_LAYER_AST_CHECK_FAILED":
            return code
    return "E_LAYER_AST_CHECK_FAILED"


def diag_expectations_by_fixture(manifest: dict) -> dict[str, dict]:
    out: dict[str, dict] = {}
    for group in manifest["normative_ast_checks"]["check_groups"]:
        if group.get("kind") != "diagnostics":
            continue
        for check in group.get("checks", []):
            assertion = check.get("assert", {})
            if assertion.get("kind") != "invalid_fixture_yields_error":
                continue
            params = assertion.get("params", {})
            fixture_id = params.get("fixture_id")
            if not fixture_id:
                continue
            out[fixture_id] = {
                "check_id": check.get("id", ""),
                "expected_code": params.get("expected_code", ""),
                "expected_blame_pointer": params.get("expected_blame_pointer", ""),
            }
    return out


def has_diag(diags: list[dict], code: str, blame_pointer: str = "", check_id: str = "") -> bool:
    for d in diags:
        if d.get("code") != code:
            continue
        if check_id and d.get("check_id") != check_id:
            continue
        if blame_pointer:
            pointers = d.get("blame_pointers", [])
            if blame_pointer not in pointers:
                continue
        return True
    return False


def main() -> int:
    if not CLI.exists():
        raise SystemExit(f"missing CLI binary: {CLI}")

    failures: list[str] = []
    out_dir = ROOT / "artifacts" / "runtime_checks"
    out_dir.mkdir(parents=True, exist_ok=True)

    for manifest_path in CONTRACTS:
        m = json.loads(manifest_path.read_text(encoding="utf-8"))
        layer_id = m["layer_id"]
        expected_error = first_primary_error(m)
        diag_expect = diag_expectations_by_fixture(m)

        for fixture in m["fixtures"]["valid"]:
            fixture_path = ROOT / fixture["path"]
            valid_jsonl = out_dir / f"{layer_id}.{fixture['id']}.valid.jsonl"
            valid_summary = out_dir / f"{layer_id}.{fixture['id']}.valid.txt"
            rc_valid = run_cli(fixture_path, valid_jsonl, valid_summary)
            if rc_valid != 0:
                failures.append(f"{layer_id}: valid fixture {fixture['id']} failed with rc={rc_valid}")

        for fixture in m["fixtures"]["invalid"]:
            fixture_id = fixture["id"]
            fixture_path = ROOT / fixture["path"]
            invalid_jsonl = out_dir / f"{layer_id}.{fixture_id}.invalid.jsonl"
            invalid_summary = out_dir / f"{layer_id}.{fixture_id}.invalid.txt"

            rc_invalid = run_cli(fixture_path, invalid_jsonl, invalid_summary)
            diags = read_jsonl(invalid_jsonl)
            if rc_invalid == 0:
                failures.append(f"{layer_id}: invalid fixture {fixture_id} unexpectedly passed")
                continue

            if not any(d.get("code") == expected_error for d in diags):
                failures.append(f"{layer_id}: invalid fixture {fixture_id} missing primary error {expected_error}")

            for expected in fixture.get("expect_errors", []):
                code = expected.get("code", "")
                pointers = expected.get("blame_pointers", [])
                blame = pointers[0] if pointers else ""
                if code and not has_diag(diags, code, blame):
                    failures.append(
                        f"{layer_id}: invalid fixture {fixture_id} missing expected diagnostic code={code} blame={blame}"
                    )

            from_diag_group = diag_expect.get(fixture_id)
            if from_diag_group:
                if not has_diag(
                    diags,
                    from_diag_group["expected_code"],
                    from_diag_group["expected_blame_pointer"],
                    from_diag_group["check_id"],
                ):
                    failures.append(
                        f"{layer_id}: invalid fixture {fixture_id} missing diagnostics-group match "
                        f"check_id={from_diag_group['check_id']} code={from_diag_group['expected_code']} "
                        f"blame={from_diag_group['expected_blame_pointer']}"
                    )

    if failures:
        for f in failures:
            print(f)
        return 2
    print("runtime fixture checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
