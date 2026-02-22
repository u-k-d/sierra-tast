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


def read_jsonl_codes(path: Path) -> list[str]:
    if not path.exists():
        return []
    codes = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        codes.append(json.loads(line)["code"])
    return codes


def main() -> int:
    if not CLI.exists():
        raise SystemExit(f"missing CLI binary: {CLI}")

    failures: list[str] = []
    out_dir = ROOT / "artifacts" / "runtime_checks"
    out_dir.mkdir(parents=True, exist_ok=True)

    for manifest_path in CONTRACTS:
        m = json.loads(manifest_path.read_text(encoding="utf-8"))
        layer_id = m["layer_id"]
        expected_error = m["public_api"]["error_codes"][0]["code"]

        valid_fixture = ROOT / m["fixtures"]["valid"][0]["path"]
        invalid_fixture = ROOT / m["fixtures"]["invalid"][0]["path"]

        valid_jsonl = out_dir / f"{layer_id}.valid.jsonl"
        valid_summary = out_dir / f"{layer_id}.valid.txt"
        invalid_jsonl = out_dir / f"{layer_id}.invalid.jsonl"
        invalid_summary = out_dir / f"{layer_id}.invalid.txt"

        rc_valid = run_cli(valid_fixture, valid_jsonl, valid_summary)
        if rc_valid != 0:
            failures.append(f"{layer_id}: valid fixture failed with rc={rc_valid}")

        rc_invalid = run_cli(invalid_fixture, invalid_jsonl, invalid_summary)
        codes = read_jsonl_codes(invalid_jsonl)
        if rc_invalid == 0:
            failures.append(f"{layer_id}: invalid fixture unexpectedly passed")
        if expected_error not in codes:
            failures.append(f"{layer_id}: expected error {expected_error} not found in diagnostics")

    if failures:
        for f in failures:
            print(f)
        return 2
    print("runtime fixture checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

