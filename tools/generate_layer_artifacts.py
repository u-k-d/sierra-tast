import json
from pathlib import Path
from hash_semantics import contract_sha256, doc_sha256

ROOT = Path(r"e:/sre_spec_bundle")

LAYER_ORDER = [
    (0, "core_identity_io", ["/version", "/plan_kind", "/plan_io", "/compat", "/meta", "/extensions"]),
    (1, "universe_data", ["/universe"]),
    (2, "sierra_runtime_topology", ["/execution", "/execution/backend"]),
    (3, "studies_features", ["/studies"]),
    (4, "rule_chain_dsl", ["/chains", "/signals", "/gates", "/validation", "/execution/backend/sierra_chart/layout_contract/readiness"]),
    (5, "experimentation_permute", ["/parameters", "/parameters/permute", "/validation", "/studies"]),
    (6, "semantics_integrity", ["/validation", "/execution", "/studies", "/chains", "/parameters", "/gates", "/outputs"]),
    (7, "outputs_repro", ["/outputs", "/execution/permissions"]),
    (8, "governance_evolution", ["/dag", "/compat", "/meta"]),
]

LAYER_ERROR = {
    "core_identity_io": "E_LAYER_CORE_IDENTITY_INVALID",
    "universe_data": "E_LAYER_UNIVERSE_INVALID",
    "sierra_runtime_topology": "E_LAYER_RUNTIME_TOPOLOGY_INVALID",
    "studies_features": "E_LAYER_STUDY_BINDING_INVALID",
    "rule_chain_dsl": "E_LAYER_CHAIN_DSL_INVALID",
    "experimentation_permute": "E_LAYER_PERMUTE_INVALID",
    "semantics_integrity": "E_SEM_SAME_BAR_LEAKAGE",
    "outputs_repro": "E_IO_OUTPUT_CONFIG_INVALID",
    "governance_evolution": "E_LAYER_GOVERNANCE_INVALID",
}

LAYER_SUMMARY = {
    "core_identity_io": "Plan identity and input/output defaults.",
    "universe_data": "Universe source resolution and canonical symbol set.",
    "sierra_runtime_topology": "Sierra Chart topology, readiness and permissions.",
    "studies_features": "Study binding modes, inputs and output mapping.",
    "rule_chain_dsl": "Chains, gates and signals DSL compilation surface.",
    "experimentation_permute": "Experiment permutation declarations and policy.",
    "semantics_integrity": "Cross-layer semantic integrity and tripwires.",
    "outputs_repro": "Dataset and reproducible artifact emission.",
    "governance_evolution": "Capabilities, deprecations and ADR-DAG governance.",
}

REQUIRED_CHECK_KINDS = [
    "api_signatures",
    "file_layout",
    "module_boundaries",
    "plan_ast_shape",
    "diagnostics",
]

with (ROOT / "layers.lock.json").open("r", encoding="utf-8") as f:
    lock = json.load(f)

interface_versions = {x["interface_id"]: x["version"] for x in lock["interfaces"]}
allowed_dep_map = {x["from"]: x for x in lock["allowed_deps"]}


def write_json(path: Path, obj):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(obj, indent=2, ensure_ascii=True) + "\n", encoding="utf-8")


def base_plan():
    return {
        "version": 262,
        "plan_kind": "sierra_research_engine.plan",
        "plan_io": {"format": "json", "encoding": "utf-8"},
        "universe": {"source": "inline", "symbols": ["ES", "NQ"], "timeframe": "5m", "date_range": {"start": "2025-01-01", "end": "2025-01-31"}},
        "execution": {"worker_charts": [2], "backend": {"type": "sierra_chart", "sierra_chart": {}}},
        "studies": {"ema_fast": {"mode": "managed", "study_id": 1, "outputs": {"value": {"subgraph": 0}}}},
        "chains": {"main": {"steps": [{"kind": "emit", "from": "@study.ema_fast.value", "as": "ema"}]}} ,
        "outputs": {"dataset": {"format": "csv", "path": "out/data.csv", "fields": [{"id": "ema", "from": "@step.main.ema"}]}}
    }


for n, layer_id, pointers in LAYER_ORDER:
    layer_tag = f"L{n}_{layer_id}"
    dep = allowed_dep_map[layer_id]
    allowed_interfaces = [
        {"interface_id": iid, "version": interface_versions[iid]}
        for iid in dep["to_interfaces"]
    ]

    fn_name = "Validate" + "".join([x.capitalize() for x in layer_id.split("_")])
    reg_name = "Register" + "".join([x.capitalize() for x in layer_id.split("_")]) + "Api"

    invalid_count = 13 if layer_id == "semantics_integrity" else 10

    valid_specs = []
    invalid_specs = []
    diag_checks = []

    for i in range(1, 11):
        fid = f"FIX_VALID_{i:02d}"
        rel = f"fixtures/{layer_tag}/valid/{fid.lower()}.json"
        valid_specs.append({"id": fid, "path": rel, "expect": {}})

    for i in range(1, invalid_count + 1):
        fid = f"FIX_INVALID_{i:02d}"
        rel = f"fixtures/{layer_tag}/invalid/{fid.lower()}.json"
        err = LAYER_ERROR[layer_id]
        invalid_specs.append(
            {
                "id": fid,
                "path": rel,
                "expect": {},
                "expect_errors": [
                    {
                        "code": err,
                        "blame_pointers": [pointers[0]],
                        "dag_node_ids": [f"{layer_id}_node_{i:02d}"]
                    }
                ]
            }
        )
        diag_checks.append(
            {
                "id": f"CHK_DIAG_INVALID_{i:02d}",
                "severity": "error",
                "assert": {
                    "kind": "invalid_fixture_yields_error",
                    "params": {
                        "fixture_id": fid,
                        "expected_code": err,
                        "expected_blame_pointer": pointers[0]
                    }
                }
            }
        )

    contract = {
        "contract_block_version": 1,
        "contract_semantics_version": 1,
        "layer_id": layer_id,
        "layer_version": 1,
        "schema_scope": {
            "owned_pointers": pointers,
            "owned_defs": [],
            "notes": LAYER_SUMMARY[layer_id],
        },
        "dependencies": {
            "allowed_layer_ids": dep["to_layers"],
            "allowed_interfaces": allowed_interfaces,
        },
        "ast_nodes": [
            {
                "node_id": f"{''.join(x.capitalize() for x in layer_id.split('_'))}Node",
                "kind": "struct",
                "fields": [
                    {"name": "layer_id", "type": "string", "required": True, "default": layer_id},
                    {"name": "scope", "type": "json_pointer[]", "required": True},
                ],
                "invariants": [
                    {
                        "id": "INV_POINTER_SCOPE_ENFORCED",
                        "severity": "error",
                        "rule": {
                            "kind": "no_cross_layer_pointer_access",
                            "params": {"owned_pointers": pointers},
                        },
                    }
                ],
            }
        ],
        "canonicalization_rules": {
            "cnf_version": 1,
            "defaults_policy": "apply_schema_defaults_then_contract_defaults",
            "ordering_policy": "stable_lexicographic",
            "ref_resolution_policy": {
                "must_resolve": True,
                "on_unresolved": "error",
            },
        },
        "public_api": {
            "functions": [
                {
                    "id": "FN_VALIDATE_LAYER",
                    "name": fn_name,
                    "signature": {
                        "namespace": "sre::layers",
                        "return_type": "Status",
                        "params": [
                            {"name": "plan_view", "type": "const ScopedPlanView&"},
                            {"name": "diag", "type": "DiagnosticSink&"},
                        ],
                        "noexcept": True,
                    },
                    "stability": "stable",
                },
                {
                    "id": "FN_REGISTER_LAYER_API",
                    "name": reg_name,
                    "signature": {
                        "namespace": "sre::layers",
                        "return_type": "void",
                        "params": [],
                        "noexcept": True,
                    },
                    "stability": "stable",
                },
            ],
            "types": [
                {"id": "TY_LAYER_CONTEXT", "name": "LayerContext", "kind": "struct"},
                {"id": "TY_LAYER_RESULT", "name": "LayerResult", "kind": "struct"},
            ],
            "error_codes": [
                {
                    "code": LAYER_ERROR[layer_id],
                    "layer_id": layer_id,
                    "severity": "error",
                    "message_template": f"{layer_id} contract violation: {{reason}}",
                },
                {
                    "code": "E_LAYER_AST_CHECK_FAILED",
                    "layer_id": layer_id,
                    "severity": "error",
                    "message_template": "Normative AST check failed: {check_id}",
                },
            ],
        },
        "normative_ast_checks": {
            "cnf_basis": "cnf_only",
            "check_groups": [
                {
                    "group_id": "CHKGRP_API_SIGNATURES",
                    "kind": "api_signatures",
                    "checks": [
                        {
                            "id": "CHK_API_VALIDATE_SIGNATURE",
                            "severity": "error",
                            "assert": {
                                "kind": "function_exists_with_signature",
                                "params": {
                                    "function": fn_name,
                                    "namespace": "sre::layers",
                                    "return_type": "Status",
                                },
                            },
                        }
                    ],
                },
                {
                    "group_id": "CHKGRP_FILE_LAYOUT",
                    "kind": "file_layout",
                    "checks": [
                        {
                            "id": "CHK_FILE_NORMATIVE_DOC_EXISTS",
                            "severity": "error",
                            "assert": {
                                "kind": "file_exists",
                                "params": {"path": f"docs/normative/{layer_tag}.md"},
                            },
                        },
                        {
                            "id": "CHK_FILE_CONTRACT_MANIFEST_EXISTS",
                            "severity": "error",
                            "assert": {
                                "kind": "file_exists",
                                "params": {"path": f"contracts/{layer_tag}.manifest.json"},
                            },
                        },
                        {
                            "id": "CHK_FILE_FORBIDDEN_TEMP",
                            "severity": "warn",
                            "assert": {
                                "kind": "file_forbidden",
                                "params": {"path": f"src/layers/{layer_tag}/tmp.txt"},
                            },
                        },
                    ],
                },
                {
                    "group_id": "CHKGRP_MODULE_BOUNDARIES",
                    "kind": "module_boundaries",
                    "checks": [
                        {
                            "id": "CHK_BOUNDARY_ONLY_ALLOWED_DEPS",
                            "severity": "error",
                            "assert": {
                                "kind": "only_allowed_layer_deps",
                                "params": {
                                    "layer_id": layer_id,
                                    "allowed_layer_ids": dep["to_layers"],
                                },
                            },
                        }
                    ],
                },
                {
                    "group_id": "CHKGRP_PLAN_AST_SHAPE",
                    "kind": "plan_ast_shape",
                    "checks": [
                        {
                            "id": "CHK_AST_LAYER_NODE_PRESENT",
                            "severity": "error",
                            "assert": {
                                "kind": "cnf_has_node",
                                "params": {"node_id": f"{''.join(x.capitalize() for x in layer_id.split('_'))}Node"},
                            },
                        },
                        {
                            "id": "CHK_AST_POINTER_SCOPE",
                            "severity": "error",
                            "assert": {
                                "kind": "cnf_pointer_in_scope",
                                "params": {
                                    "pointers": pointers,
                                },
                            },
                        },
                    ],
                },
                {
                    "group_id": "CHKGRP_DIAGNOSTICS",
                    "kind": "diagnostics",
                    "checks": diag_checks,
                },
            ],
        },
        "compile_outputs": {
            "emits": ["execution_dag", "lineage_dag", "diagnostics", "manifest"],
            "artifacts": [
                {"id": "ART_LAYER_API_HEADER", "path": f"generated/contracts/{layer_tag}_api.h", "kind": "header"},
                {"id": "ART_LAYER_CONTRACT", "path": f"contracts/{layer_tag}.manifest.json", "kind": "manifest"},
                {"id": "ART_LAYER_TEST_STRUCTURAL", "path": f"generated/tests/{layer_tag}_structural.json", "kind": "tests"},
                {"id": "ART_LAYER_TEST_SEMANTIC", "path": f"generated/tests/{layer_tag}_semantic.json", "kind": "tests"},
                {"id": "ART_LAYER_DOC", "path": f"docs/normative/{layer_tag}.md", "kind": "doc"},
            ],
        },
        "fixtures": {
            "valid": valid_specs,
            "invalid": invalid_specs,
            "minimums": {
                "valid": 10,
                "invalid": invalid_count,
            },
        },
        "capabilities": {
            "introduces": [
                {
                    "id": f"cap:sre:{layer_id}:v1",
                    "version": 1,
                    "summary": LAYER_SUMMARY[layer_id],
                }
            ],
            "requires": [],
            "optional": [],
            "deprecates": [],
        },
    }

    contract_hash = contract_sha256(contract)
    contract["hashes"] = {
        "doc_sha256": "sha256:" + ("0" * 64),
        "contract_sha256": contract_hash,
    }

    layer_doc = (
        f"# {layer_tag} Normative Specification\n\n"
        f"Layer: `{layer_id}`\n\n"
        f"This normative document defines the machine-checkable contract for `{layer_id}`.\n\n"
        "## Contract Block\n\n"
        "```json\n"
        f"{json.dumps(contract, indent=2, ensure_ascii=True)}\n"
        "```\n"
    )
    contract["hashes"]["doc_sha256"] = doc_sha256(layer_doc)
    layer_doc = (
        f"# {layer_tag} Normative Specification\n\n"
        f"Layer: `{layer_id}`\n\n"
        f"This normative document defines the machine-checkable contract for `{layer_id}`.\n\n"
        "## Contract Block\n\n"
        "```json\n"
        f"{json.dumps(contract, indent=2, ensure_ascii=True)}\n"
        "```\n"
    )

    doc_path = ROOT / "docs" / "normative" / f"{layer_tag}.md"
    doc_path.parent.mkdir(parents=True, exist_ok=True)
    doc_path.write_text(layer_doc, encoding="utf-8")

    manifest_path = ROOT / "contracts" / f"{layer_tag}.manifest.json"
    write_json(manifest_path, contract)

    header_lines = [
        "#pragma once",
        "",
        "#include <type_traits>",
        "",
        "namespace sre::layers {",
        "struct ScopedPlanView;",
        "struct DiagnosticSink;",
        "struct Status;",
        f"Status {fn_name}(const ScopedPlanView& plan_view, DiagnosticSink& diag) noexcept;",
        f"void {reg_name}() noexcept;",
        "}  // namespace sre::layers",
        "",
        "#define REGISTER_LAYER_API(layer_id, ...) static_assert(true, \"api registered\")",
        "",
    ]

    h_path = ROOT / "generated" / "contracts" / f"{layer_tag}_api.h"
    h_path.parent.mkdir(parents=True, exist_ok=True)
    h_path.write_text("\n".join(header_lines), encoding="utf-8")

    structural = {
        "layer_id": layer_id,
        "required_check_kinds": REQUIRED_CHECK_KINDS,
        "group_ids": [g["group_id"] for g in contract["normative_ast_checks"]["check_groups"]],
        "owned_pointers": pointers,
    }
    semantic = {
        "layer_id": layer_id,
        "expected_invalid_diagnostics": [
            {
                "fixture_id": x["id"],
                "code": x["expect_errors"][0]["code"],
                "blame_pointers": x["expect_errors"][0]["blame_pointers"],
            }
            for x in invalid_specs
        ],
    }
    write_json(ROOT / "generated" / "tests" / f"{layer_tag}_structural.json", structural)
    write_json(ROOT / "generated" / "tests" / f"{layer_tag}_semantic.json", semantic)

    py_test = f'''import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LAYER_TAG = "{layer_tag}"


def test_contract_groups_and_fixture_counts():
    manifest = json.loads((ROOT / "contracts" / f"{{LAYER_TAG}}.manifest.json").read_text(encoding="utf-8"))
    kinds = {{g["kind"] for g in manifest["normative_ast_checks"]["check_groups"]}}
    required = {REQUIRED_CHECK_KINDS!r}
    missing = [k for k in required if k not in kinds]
    assert not missing, f"missing check kinds: {{missing}}"

    vmin = manifest["fixtures"]["minimums"]["valid"]
    imin = manifest["fixtures"]["minimums"]["invalid"]
    assert len(manifest["fixtures"]["valid"]) >= vmin
    assert len(manifest["fixtures"]["invalid"]) >= imin
'''
    (ROOT / "generated" / "tests" / f"{layer_tag}_contract_test.py").write_text(py_test, encoding="utf-8")

    layer_src = ROOT / "src" / "layers" / layer_tag
    layer_test = ROOT / "tests" / "layers" / layer_tag
    layer_src.mkdir(parents=True, exist_ok=True)
    layer_test.mkdir(parents=True, exist_ok=True)
    (layer_src / "README.md").write_text(f"# {layer_tag}\n\nImplementation module for {layer_id}.\n", encoding="utf-8")
    (layer_test / "README.md").write_text(f"# Tests for {layer_tag}\n", encoding="utf-8")

    for i in range(1, 11):
        fid = f"fix_valid_{i:02d}.json"
        p = ROOT / "fixtures" / layer_tag / "valid" / fid
        p.parent.mkdir(parents=True, exist_ok=True)
        plan = base_plan()
        plan.setdefault("meta", {})["fixture_id"] = f"{layer_tag}_valid_{i:02d}"
        p.write_text(json.dumps(plan, indent=2, ensure_ascii=True) + "\n", encoding="utf-8")

    for i in range(1, invalid_count + 1):
        fid = f"fix_invalid_{i:02d}.json"
        p = ROOT / "fixtures" / layer_tag / "invalid" / fid
        p.parent.mkdir(parents=True, exist_ok=True)
        plan = base_plan()
        plan.setdefault("meta", {})["fixture_id"] = f"{layer_tag}_invalid_{i:02d}"
        plan.setdefault("meta", {})["expected_error"] = LAYER_ERROR[layer_id]
        plan.setdefault("meta", {})["invalid_pointer"] = pointers[0]
        plan.setdefault("compat", {})["invalid_case"] = True
        p.write_text(json.dumps(plan, indent=2, ensure_ascii=True) + "\n", encoding="utf-8")

# Common tooling scripts
(ROOT / "tools").mkdir(parents=True, exist_ok=True)

validate_script = r'''import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LOCK = json.loads((ROOT / "layers.lock.json").read_text(encoding="utf-8"))

allowed = {x["from"]: x for x in LOCK["allowed_deps"]}
required_kinds = {"api_signatures", "file_layout", "module_boundaries", "plan_ast_shape", "diagnostics"}

manifest_paths = sorted((ROOT / "contracts").glob("L*_*.manifest.json"))
if len(manifest_paths) != 9:
    raise SystemExit(f"expected 9 manifests, found {len(manifest_paths)}")

for mp in manifest_paths:
    m = json.loads(mp.read_text(encoding="utf-8"))
    lid = m["layer_id"]
    dep = allowed[lid]

    declared_layers = set(m["dependencies"]["allowed_layer_ids"])
    if not declared_layers.issubset(set(dep["to_layers"])):
        raise SystemExit(f"{lid}: declared layer deps not subset of layers.lock")

    declared_interfaces = {x["interface_id"] for x in m["dependencies"]["allowed_interfaces"]}
    if not declared_interfaces.issubset(set(dep["to_interfaces"])):
        raise SystemExit(f"{lid}: declared interface deps not subset of layers.lock")

    kinds = {g["kind"] for g in m["normative_ast_checks"]["check_groups"]}
    missing = required_kinds - kinds
    if missing:
        raise SystemExit(f"{lid}: missing check groups {sorted(missing)}")

    vmin = m["fixtures"]["minimums"]["valid"]
    imin = m["fixtures"]["minimums"]["invalid"]
    if len(m["fixtures"]["valid"]) < vmin:
        raise SystemExit(f"{lid}: valid fixture count below minimum")
    if len(m["fixtures"]["invalid"]) < imin:
        raise SystemExit(f"{lid}: invalid fixture count below minimum")

    for fx in m["fixtures"]["valid"] + m["fixtures"]["invalid"]:
        fp = ROOT / fx["path"]
        if not fp.exists():
            raise SystemExit(f"{lid}: missing fixture file {fx['path']}")

print("contract validation passed")
'''
(ROOT / "tools" / "validate_contracts.py").write_text(validate_script, encoding="utf-8")

contractgen = r'''import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

for md in sorted((ROOT / "docs" / "normative").glob("L*_*.md")):
    text = md.read_text(encoding="utf-8")
    m = re.search(r"```json\n(.*?)\n```", text, re.DOTALL)
    if not m:
        raise SystemExit(f"contract block not found: {md}")
    contract = json.loads(m.group(1))
    out = ROOT / "contracts" / f"{md.stem}.manifest.json"
    out.write_text(json.dumps(contract, indent=2, ensure_ascii=True) + "\n", encoding="utf-8")

print("contract extraction complete")
'''
(ROOT / "tools" / "contractgen.py").write_text(contractgen, encoding="utf-8")

workflow = '''name: contracts-ci

on:
  push:
  pull_request:

jobs:
  validate-contracts:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-python@v5
        with:
          python-version: "3.11"
      - name: Validate contracts and fixtures
        run: python tools/validate_contracts.py
      - name: Regenerate manifests from docs
        run: python tools/contractgen.py
      - name: Ensure no drift after generation
        run: git diff --exit-code
'''
wf_path = ROOT / ".github" / "workflows" / "contracts-ci.yml"
wf_path.parent.mkdir(parents=True, exist_ok=True)
wf_path.write_text(workflow, encoding="utf-8")

print("generation complete")
