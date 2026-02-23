import json
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CLI = ROOT / "build" / "sre_planlint.exe"
BASE_FIXTURE = ROOT / "fixtures" / "L0_core_identity_io" / "valid" / "fix_valid_01.json"


def run_plan(plan: dict, tmp_dir: Path, name: str) -> tuple[int, Path, Path]:
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
    return proc.returncode, jsonl_path, txt_path


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
        c1.setdefault("outputs", {})["dataset"] = {
            "format": "csv",
            "path": str(out_dir / "data.csv"),
            "fields": [{"name": "symbol", "ref": "@symbol"}],
        }
        c1.setdefault("outputs", {}).setdefault("artifacts", {})
        c1["outputs"]["artifacts"]["enabled"] = True
        c1["outputs"]["artifacts"]["base_dir"] = str(out_dir)
        c1["outputs"]["artifacts"]["write_run_manifest"] = True
        c1["outputs"]["artifacts"]["write_metrics_summary"] = True
        rc, _, _ = run_plan(c1, tmp, "artifacts_enabled")
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
        rc, _, _ = run_plan(c2, tmp, "artifacts_no_permission")
        if rc != 0:
            failures.append("artifacts_no_permission plan should pass validation")
        if out_dir2.exists() and any(out_dir2.iterdir()):
            failures.append("artifacts should not be emitted when allow_filesystem_write=false")

        out_dir3 = tmp / "pipeline_outputs"
        c3 = json.loads(json.dumps(base))
        c3.setdefault("execution", {})["permissions"] = {"allow_filesystem_write": True}
        c3.setdefault("universe", {})["symbols"] = ["AAA", "BBB"]
        c3.setdefault("outputs", {})["dataset"] = {
            "format": "csv",
            "path": str(out_dir3 / "{symbol}" / "layer2_authoritative.csv"),
            "fields": [
                {"name": "symbol", "ref": "@symbol"},
                {"name": "bar_datetime", "ref": "@bar.datetime"},
                {"name": "close", "ref": "@bar.close"},
                {"name": "ema", "from": "@step.main.ema"},
            ],
        }
        c3["outputs"]["layer3"] = {
            "enabled": True,
            "inputs": {"layer2_authoritative_csv": str(out_dir3 / "{symbol}" / "layer2_authoritative.csv")},
            "eligibility": {"rules": [{"id": "min_samples"}, {"id": "ev_floor"}]},
            "rr_menu": {"risk_quantiles": [0.8, 0.9], "reward_quantiles": [0.5, 0.8]},
            "artifacts": {
                "rr_buckets": str(out_dir3 / "{symbol}" / "layer3_rr_buckets.csv"),
                "decision_audit": str(out_dir3 / "{symbol}" / "layer3_decision_audit.csv"),
            },
        }
        c3.setdefault("outputs", {}).setdefault("artifacts", {})
        c3["outputs"]["artifacts"]["enabled"] = True
        c3["outputs"]["artifacts"]["base_dir"] = str(out_dir3 / "meta")
        rc, c3_jsonl, _ = run_plan(c3, tmp, "pipeline_outputs")
        if rc == 0:
            failures.append("pipeline_outputs plan should fail with E_NOT_IMPLEMENTED diagnostics")
        if not c3_jsonl.exists():
            failures.append("pipeline_outputs should emit diagnostics jsonl")
        else:
            diags = []
            for line in c3_jsonl.read_text(encoding="utf-8").splitlines():
                line = line.strip()
                if line:
                    diags.append(json.loads(line))
            if not any(d.get("code") == "E_NOT_IMPLEMENTED" for d in diags):
                failures.append("pipeline_outputs should include E_NOT_IMPLEMENTED diagnostic")
            if not any(d.get("check_id") == "CHK_RUNTIME_LAYER3_OUTPUT_NOT_IMPLEMENTED" for d in diags):
                failures.append("pipeline_outputs should include CHK_RUNTIME_LAYER3_OUTPUT_NOT_IMPLEMENTED diagnostic")
            if not any(d.get("check_id") == "CHK_RUNTIME_DATASET_REF_NOT_IMPLEMENTED" for d in diags):
                failures.append("pipeline_outputs should include CHK_RUNTIME_DATASET_REF_NOT_IMPLEMENTED diagnostic")

    if failures:
        for f in failures:
            print(f)
        return 2
    print("artifact emission checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
