import json
import csv
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CLI = ROOT / "build" / "sre_planlint.exe"
BASE_FIXTURE = ROOT / "fixtures" / "L0_core_identity_io" / "valid" / "fix_valid_01.json"

L2_FIX = ROOT / "fixtures" / "augmentation" / "layer2"
L3_FIX = ROOT / "fixtures" / "augmentation" / "layer3"


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


def read_jsonl(path: Path) -> list[dict]:
    if not path.exists():
        return []
    rows: list[dict] = []
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line:
            rows.append(json.loads(line))
    return rows


def has_code(diags: list[dict], code: str) -> bool:
    return any(d.get("code") == code for d in diags)


def load_fixture_template(path: Path, tmp_root: Path) -> dict:
    text = path.read_text(encoding="utf-8")
    text = text.replace("__TMP__", tmp_root.as_posix())
    return json.loads(text)


def csv_row_count(path: Path) -> int:
    if not path.exists():
        return 0
    lines = [ln for ln in path.read_text(encoding="utf-8").splitlines() if ln.strip()]
    if len(lines) <= 1:
        return 0
    return len(lines) - 1


def read_csv_rows(path: Path) -> tuple[list[str], list[dict[str, str]]]:
    if not path.exists():
        return [], []
    with path.open("r", encoding="utf-8", newline="") as f:
        reader = csv.DictReader(f)
        return list(reader.fieldnames or []), list(reader)


def main() -> int:
    if not CLI.exists():
        raise SystemExit(f"missing binary: {CLI}")

    base = json.loads(BASE_FIXTURE.read_text(encoding="utf-8"))
    failures: list[str] = []

    with tempfile.TemporaryDirectory(prefix="sre_artifacts_") as td:
        tmp = Path(td)

        # Legacy success path remains unchanged.
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

        # Legacy behavior remains: no explicit permission means no filesystem emission.
        out_dir2 = tmp / "artifacts_disabled_by_permission"
        c2 = json.loads(json.dumps(base))
        c2.setdefault("outputs", {}).setdefault("artifacts", {})
        c2["outputs"]["artifacts"]["enabled"] = True
        c2["outputs"]["artifacts"]["base_dir"] = str(out_dir2)
        rc, _, _ = run_plan(c2, tmp, "artifacts_no_permission")
        if rc != 0:
            failures.append("artifacts_no_permission plan should pass validation")
        if out_dir2.exists() and any(out_dir2.iterdir()):
            failures.append("artifacts should not be emitted when allow_filesystem_write=false")

        # Legacy rr_menu path remains unchanged (still not implemented in this runtime path).
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
        diags = read_jsonl(c3_jsonl)
        if not has_code(diags, "E_NOT_IMPLEMENTED"):
            failures.append("pipeline_outputs should include E_NOT_IMPLEMENTED diagnostic")

        # Layer2: valid DAG + event emission fixture.
        l2_valid = load_fixture_template(L2_FIX / "l2_valid_event_emission.json", tmp)
        rc, j, _ = run_plan(l2_valid, tmp, "l2_valid_event_emission")
        if rc != 0:
            failures.append("l2_valid_event_emission should pass")
        for symbol in ["ES", "NQ"]:
            out_csv = tmp / "l2" / symbol / "authoritative.csv"
            if csv_row_count(out_csv) <= 0:
                failures.append(f"l2_valid_event_emission should write rows for {symbol}")
        if read_jsonl(j):
            # Should be clean for valid fixture.
            failures.append("l2_valid_event_emission should not emit diagnostics")

        # Layer2 invalid diagnostics.
        for fixture_name, expected_code in [
            ("l2_invalid_cycle.json", "E_L2_DAG_CYCLE"),
            ("l2_invalid_missing_dep.json", "E_L2_MISSING_NODE_DEP"),
            ("l2_invalid_unknown_kind.json", "E_L2_UNKNOWN_NODE_KIND"),
            ("l2_invalid_dataset_ref_scope_mismatch.json", "E_DATASET_REF_SCOPE_MISMATCH"),
            ("l2_invalid_event_column_missing.json", "E_EVENT_EMITTER_MISSING_COLUMN"),
        ]:
            plan = load_fixture_template(L2_FIX / fixture_name, tmp)
            rc, jsonl, _ = run_plan(plan, tmp, fixture_name.replace(".json", ""))
            if rc == 0:
                failures.append(f"{fixture_name} should fail")
            diags = read_jsonl(jsonl)
            if not has_code(diags, expected_code):
                failures.append(f"{fixture_name} should include {expected_code}")

        # Layer2 mode semantics.
        l2_on_true = load_fixture_template(L2_FIX / "l2_mode_on_true.json", tmp)
        l2_on_edge = load_fixture_template(L2_FIX / "l2_mode_on_true_edge.json", tmp)
        rc_true, _, _ = run_plan(l2_on_true, tmp, "l2_mode_on_true")
        rc_edge, _, _ = run_plan(l2_on_edge, tmp, "l2_mode_on_true_edge")
        if rc_true != 0 or rc_edge != 0:
            failures.append("layer2 emit_mode fixtures should pass")
        rows_true = csv_row_count(tmp / "l2_mode" / "on_true.csv")
        rows_edge = csv_row_count(tmp / "l2_mode" / "on_true_edge.csv")
        if rows_edge != 1:
            failures.append(f"on_true_edge should emit exactly one row for always-true trigger, got {rows_edge}")
        if rows_true <= rows_edge:
            failures.append(f"on_true should emit more rows than on_true_edge, got on_true={rows_true}, edge={rows_edge}")

        # Layer2 date range enforcement.
        l2_date = load_fixture_template(L2_FIX / "l2_date_range_enforced.json", tmp)
        rc, _, _ = run_plan(l2_date, tmp, "l2_date_range_enforced")
        if rc != 0:
            failures.append("l2_date_range_enforced should pass")
        for symbol in ["ES", "NQ"]:
            out_csv = tmp / "l2_date" / symbol / "authoritative.csv"
            if csv_row_count(out_csv) != 0:
                failures.append(f"l2_date_range_enforced should emit zero rows for {symbol}")

        l2_date_invalid = load_fixture_template(L2_FIX / "l2_invalid_date_range_leakage.json", tmp)
        rc, jsonl, _ = run_plan(l2_date_invalid, tmp, "l2_invalid_date_range_leakage")
        if rc == 0:
            failures.append("l2_invalid_date_range_leakage should fail")
        if not has_code(read_jsonl(jsonl), "E_L2_DATE_RANGE_VIOLATION"):
            failures.append("l2_invalid_date_range_leakage should emit E_L2_DATE_RANGE_VIOLATION")

        # Layer3 valid outcomes + bucketing + EV.
        l3_valid = load_fixture_template(L3_FIX / "l3_valid_outcomes_bucketing.json", tmp)
        rc, _, _ = run_plan(l3_valid, tmp, "l3_valid_outcomes_bucketing")
        if rc != 0:
            failures.append("l3_valid_outcomes_bucketing should pass")
        for symbol in ["ES", "NQ"]:
            out_events = tmp / "layer3" / symbol / "outcomes_per_event.csv"
            out_buckets = tmp / "layer3" / symbol / "bucket_stats.csv"
            out_audit = tmp / "layer3" / symbol / "decision_audit.csv"
            if csv_row_count(out_events) <= 0:
                failures.append(f"layer3 outcomes_per_event missing rows for {symbol}")
            if csv_row_count(out_buckets) <= 0:
                failures.append(f"layer3 bucket_stats missing rows for {symbol}")
            if csv_row_count(out_audit) <= 0:
                failures.append(f"layer3 decision_audit missing rows for {symbol}")

            event_headers, event_rows = read_csv_rows(out_events)
            required_event_cols = {"horizon_bars", "side", "mfe_pct", "mfa_pct", "ret_pct", "net_ret_pct"}
            if not required_event_cols.issubset(set(event_headers)):
                failures.append(f"outcomes_per_event missing expected columns for {symbol}")
            horizons = {r.get("horizon_bars", "") for r in event_rows}
            if horizons != {"5", "10", "15", "20"}:
                failures.append(f"outcomes_per_event should include horizons 5/10/15/20 for {symbol}, got {sorted(horizons)}")
            sides = {r.get("side", "") for r in event_rows}
            if sides != {"long", "short"}:
                failures.append(f"outcomes_per_event should include long+short for {symbol}, got {sorted(sides)}")

            pairs: dict[tuple[str, str, str], dict[str, dict[str, str]]] = {}
            for row in event_rows:
                key = (row.get("event_index", ""), row.get("event_bar_index", ""), row.get("horizon_bars", ""))
                pairs.setdefault(key, {})[row.get("side", "")] = row
            checked_pair = False
            for pair_rows in pairs.values():
                if "long" not in pair_rows or "short" not in pair_rows:
                    continue
                try:
                    l_ret = float(pair_rows["long"]["ret_pct"])
                    s_ret = float(pair_rows["short"]["ret_pct"])
                    l_mfe = float(pair_rows["long"]["mfe_pct"])
                    s_mfa = float(pair_rows["short"]["mfa_pct"])
                    l_mfa = float(pair_rows["long"]["mfa_pct"])
                    s_mfe = float(pair_rows["short"]["mfe_pct"])
                except Exception:
                    continue
                checked_pair = True
                if abs(l_ret + s_ret) > 1e-9:
                    failures.append(f"long/short ret symmetry failed for {symbol}")
                    break
                if abs(l_mfe + s_mfa) > 1e-9:
                    failures.append(f"long/short mfe/mfa symmetry failed for {symbol}")
                    break
                if abs(l_mfa + s_mfe) > 1e-9:
                    failures.append(f"long/short mfa/mfe symmetry failed for {symbol}")
                    break
            if not checked_pair:
                failures.append(f"no comparable long/short event pairs for {symbol}")

            bucket_headers, bucket_rows = read_csv_rows(out_buckets)
            required_bucket_cols = {
                "horizon_bars",
                "side",
                "rvol_min",
                "rvol_max",
                "rvol_lo",
                "rvol_hi",
                "n_trades",
                "EV",
            }
            if not required_bucket_cols.issubset(set(bucket_headers)):
                failures.append(f"bucket_stats missing expected columns for {symbol}")
            bucket_horizons = {r.get("horizon_bars", "") for r in bucket_rows}
            if "5" not in bucket_horizons or "20" not in bucket_horizons:
                failures.append(f"bucket_stats horizon coverage is incomplete for {symbol}")
            bucket_sides = {r.get("side", "") for r in bucket_rows}
            if bucket_sides != {"long", "short"}:
                failures.append(f"bucket_stats should include long+short for {symbol}, got {sorted(bucket_sides)}")

            audit_headers, _ = read_csv_rows(out_audit)
            required_audit_cols = {"context_id", "horizon_bars", "side", "rule_id", "result"}
            if not required_audit_cols.issubset(set(audit_headers)):
                failures.append(f"decision_audit missing side/horizon context columns for {symbol}")

        # Layer3 same-bar leakage rejection.
        l3_invalid = load_fixture_template(L3_FIX / "l3_invalid_same_bar_leakage.json", tmp)
        rc, jsonl, _ = run_plan(l3_invalid, tmp, "l3_invalid_same_bar_leakage")
        if rc == 0:
            failures.append("l3_invalid_same_bar_leakage should fail")
        if not has_code(read_jsonl(jsonl), "E_L3_OUTCOME_LEAKAGE_SAME_BAR"):
            failures.append("l3_invalid_same_bar_leakage should emit E_L3_OUTCOME_LEAKAGE_SAME_BAR")

        # Determinism: bucket_stats stable across identical runs.
        run_a_root = tmp / "det_a"
        run_b_root = tmp / "det_b"
        det_a = load_fixture_template(L3_FIX / "l3_valid_outcomes_bucketing.json", run_a_root)
        det_b = load_fixture_template(L3_FIX / "l3_valid_outcomes_bucketing.json", run_b_root)
        rc_a, _, _ = run_plan(det_a, tmp, "l3_det_a")
        rc_b, _, _ = run_plan(det_b, tmp, "l3_det_b")
        if rc_a != 0 or rc_b != 0:
            failures.append("determinism runs should pass")
        bucket_a = run_a_root / "layer3" / "ES" / "bucket_stats.csv"
        bucket_b = run_b_root / "layer3" / "ES" / "bucket_stats.csv"
        if not bucket_a.exists() or not bucket_b.exists():
            failures.append("determinism runs missing bucket_stats artifacts")
        else:
            if bucket_a.read_text(encoding="utf-8") != bucket_b.read_text(encoding="utf-8"):
                failures.append("bucket_stats should be identical across deterministic runs")

    if failures:
        for f in failures:
            print(f)
        return 2
    print("artifact emission checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
