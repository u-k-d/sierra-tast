import json
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CLI = ROOT / "build" / "sre_planlint.exe"
BASE_FIXTURE = ROOT / "fixtures" / "L0_core_identity_io" / "valid" / "fix_valid_01.json"


def run_plan(plan: dict, tmp_dir: Path, name: str) -> tuple[int, Path]:
    plan_path = tmp_dir / f"{name}.json"
    jsonl_path = tmp_dir / f"{name}.jsonl"
    txt_path = tmp_dir / f"{name}.txt"
    plan_path.write_text(json.dumps(plan, indent=2) + "\n", encoding="utf-8")
    proc = subprocess.run(
        [str(CLI), str(plan_path), str(jsonl_path), str(txt_path)],
        cwd=ROOT,
        capture_output=True,
        text=True,
    )
    return proc.returncode, tmp_dir


def main() -> int:
    if not CLI.exists():
        raise SystemExit(f"missing binary: {CLI}")

    base = json.loads(BASE_FIXTURE.read_text(encoding="utf-8"))
    failures: list[str] = []

    with tempfile.TemporaryDirectory(prefix="sre_artifacts_") as td:
        tmp = Path(td)
        out_dir = tmp / "artifacts_enabled"
        c1 = json.loads(json.dumps(base))
        c1.setdefault("execution", {})["permissions"] = {"allow_filesystem_write": True}
        c1.setdefault("outputs", {}).setdefault("artifacts", {})
        c1["outputs"]["artifacts"]["enabled"] = True
        c1["outputs"]["artifacts"]["base_dir"] = str(out_dir)
        c1["outputs"]["artifacts"]["write_run_manifest"] = True
        c1["outputs"]["artifacts"]["write_metrics_summary"] = True
        rc, _ = run_plan(c1, tmp, "artifacts_enabled")
        if rc != 0:
            failures.append("artifacts_enabled plan should pass")
        for expected in [
            out_dir / "execution_dag.json",
            out_dir / "lineage_dag.json",
            out_dir / "run_manifest.json",
            out_dir / "metrics_summary.json",
        ]:
            if not expected.exists():
                failures.append(f"missing artifact file: {expected}")
        if (out_dir / "run_manifest.json").exists():
            manifest = json.loads((out_dir / "run_manifest.json").read_text(encoding="utf-8"))
            for k in ["plan_cnf_hash", "execution_dag_hash", "lineage_dag_hash"]:
                v = manifest.get(k, "")
                if not (isinstance(v, str) and v.startswith("sha256:") and len(v) == 71):
                    failures.append(f"invalid hash field in run manifest: {k}={v}")

        out_dir2 = tmp / "artifacts_disabled_by_permission"
        c2 = json.loads(json.dumps(base))
        # Keep permissions unspecified: runtime should not emit artifacts without explicit write permission.
        c2.setdefault("outputs", {}).setdefault("artifacts", {})
        c2["outputs"]["artifacts"]["enabled"] = True
        c2["outputs"]["artifacts"]["base_dir"] = str(out_dir2)
        rc, _ = run_plan(c2, tmp, "artifacts_no_permission")
        if rc != 0:
            failures.append("artifacts_no_permission plan should pass validation")
        if out_dir2.exists() and any(out_dir2.iterdir()):
            failures.append("artifacts should not be emitted when allow_filesystem_write=false")

    if failures:
        for f in failures:
            print(f)
        return 2
    print("artifact emission checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
