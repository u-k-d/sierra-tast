# Sierra Research Engine — Implementation Plan (Schema-Layered, Drift-Proof)

Date: 2026-02-23  
Owner: Principal Developer (Hardcore)  
Scope: Implement the **Sierra Research Engine Plan Bundle (Sierra Runtime) v2.6.2** as a modular, layered system with **contract-driven development**, **AST+invariant test suites**, and **traceable evolution via ADR DAG**.

---

## Goals

- **Modular layers** with meaningful boundaries: independently testable, upgradeable, and reviewable.
- **No drift** between schema, normative docs, contracts, and code:
  - Normative docs contain a **machine-readable contract block**.
  - Contracts generate **AST + invariant tests** and **compile-time API checks**.
- **Research integrity** is first-class:
  - Semantic validator catches subtle failure modes (leakage, staleness, dedupe issues).
- **Traceability**:
  - Executive decisions recorded as **ADR DAG** nodes linked to schema pointers and contracts.

---

## Non-Goals (Initial Iteration)

- No external network access in runtime.
- No distributed execution outside Sierra Chart instance.
- No full-blown GUI authoring tool (optional future work).

---

## Planes & Bridges (Hard Boundaries)

### Planes
1) **Spec Plane (S)**  
   JSON Schemas split into layers; minimal metadata for doc/contract refs + hashes.
2) **Contract Plane (C)**  
   Machine-checkable contract manifests per layer + generated headers/tests.
3) **Implementation Plane (I)**  
   C++ (ACSIL) runtime + test harness + CLI tools (optional) for compilation/validation.

### Bridges (CI-enforced)
- **S → C**: every layer schema references `LayerSpecRef` that points to normative doc + contract manifest (+ hashes).
- **C → I**: contract manifest generates:
  - API headers (compile-time signature checks)
  - AST fixtures + invariant tests
  - error code registry
- **I must compile only if** layer API registration matches generated headers.

---

## Layering Model (Strata) — Meaningful Names

| Layer | ID | Focus | Owns Schema Pointers (examples) |
|---|---|---|---|
| L0 | `core_identity_io` | versioning, plan_kind, plan_io, meta/compat/extensions, top-level defaults | `/version`, `/plan_kind`, `/plan_io`, `/meta`, `/compat`, `/extensions` |
| L1 | `universe_data` | universe source (inline/file), timeframe, date_range | `/universe` |
| L2 | `sierra_runtime_topology` | controller/worker charts, backend contract, readiness, permissions | `/execution`, `/execution/backend` |
| L3 | `studies_features` | studies binding, modes, inputs/outputs mapping, bind policies | `/studies` (+ validation policies for study input mutation) |
| L4 | `rule_chain_dsl` | chains/steps, gates, signals DSL (pure) | `/chains`, `/signals`, `/gates` |
| L5 | `experimentation_permute` | parameters.permute (study_input/rule_param/symbol) | `/parameters` |
| L6 | `semantics_integrity` | semantic validation (leakage, staleness, dedupe, mutation drift) | semantic layer (cross-cutting) |
| L7 | `outputs_repro` | dataset outputs, artifacts, manifests/metrics | `/outputs` |
| L8 | `governance_evolution` | ADR DAG, capabilities, deprecations | `/dag` (decision DAG), plan header capability requirements |

> Note: L6 is cross-cutting but implemented as a distinct module with explicit contracts and test fixtures.

---

## Schema Refactoring Deliverables

### 1) Layer Schemas
- Create: `schema/layers/L0_core_identity_io.schema.json` … `L8_governance_evolution.schema.json`
- Create bundle schema: `schema/plan.bundle.schema.json` that composes layers via `$ref`.

### 2) Add `LayerSpecRef` in schema (per layer)
Each layer in the bundle includes metadata (not prose):
- `normative_doc_ref` (path/URI)
- `normative_doc_hash` (sha256)
- `contract_manifest_ref`
- `contract_manifest_hash`
- `generated_tests_ref`
- `generated_tests_hash`
- `layer_id`, `layer_version`, `capabilities_introduced`

---

## Normative Documentation (Drift-Proof)

### Normative Implementation Doc (per layer)
Path: `docs/normative/L{N}_{layer_id}.md`

Each doc contains:
1) **Human-readable section** (intent, rationale, examples)
2) **Normative Contract Block** (machine-readable YAML/JSON fenced block)

### Contract Block Contents (required)
- `layer_id`, `layer_version`
- `schema_scope`: owned JSON pointers
- `ast_nodes`: node definitions + invariants
- `canonicalization_rules`
- `public_api`: functions + signatures + error codes
- `normative_ast_checks`: **drift-proof checks** that are treated as spec and generate tests (see below)
- `compile_outputs`: DAG nodes, lineage, artifacts
- `fixtures`: valid/invalid + expected diagnostics
- `capabilities`: required/optional + deprecations


---

## Contract Manifests (Machine-Checkable)

Path: `contracts/L{N}_{layer_id}.manifest.json`

Generated/extracted from the contract block; used by CI to enforce:
- API signatures
- error code registry completeness
- fixture coverage

---
## Normative AST Checks (Spec-Level, CI-Enforced)

Each layer’s Normative Contract Block **must** include `normative_ast_checks`. These are not “nice to have tests” — they are **normative requirements** that generate automated checks, preventing drift between the plan, docs, and code.
### Enforcement inputs (authoritative)
- `contracts/meta.contract.schema.json`: validates the structure and required fields of each Normative Contract Block.
- `schema/layers/layers.lock.json`: the **dependency and boundary allowlist** used by CI/build tooling. Contract `dependencies.*` must be a **subset** of this lock.



### What “AST” means here
- **Plan AST checks**: validate the parsed + canonicalized (CNF) representation of a plan.
- **Implementation AST checks**: validate the **code surface** promised by the normative doc (API signatures, file layout, module boundaries). This is enforced by generated manifests + headers + repository-structure tests.

### Diagnostic Requirements (No Silent Failures)

All AST-level checks **must** emit structured diagnostics with a concrete reason. “Fail/Pass” without evidence is forbidden.

Each check (`CHK_*`) must include:
- `reason`: a short, specific statement of what violated the spec (human-readable).
- `evidence`: machine-readable `{expected, actual}` (or `{missing, found}`) fields.
- `blame_pointers`: JSON Pointer(s) into the **CNF** plan when applicable.
- `source`: where the rule came from (layer_id, contract id, doc ref).
- `remediation`: one actionable suggestion to fix the issue.
- `severity`: `error` or `warn`.
- `check_id` and `group_id`: stable IDs for grep-ability and regression.

Minimum diagnostic payload shape (normative):
- `code`: `E_LAYER_AST_CHECK_FAILED` (or a more specific `E_*`)
- `layer_id`
- `check_id`
- `group_id`
- `reason`
- `evidence`
- `blame_pointers`
- `remediation`

CI output requirements:
- Emit **JSONL** diagnostics (`artifacts/diagnostics.ast_checks.jsonl`) for machines.
- Emit a **human summary** grouped by layer → group → check for developers.
- Include `expected vs actual` in the human summary for fast debugging.

Examples of “specific reason” evidence:
- Function signature drift: expected `ReturnType fn(A,B)` but found `ReturnType fn(A,B,C)`
- File missing: required `src/layers/L3_studies_features/bind.cpp` not found
- Forbidden include: `#include "src/layers/L7_outputs_repro/..."` in L3 file
- Pointer out of scope: layer tried to access `/outputs/dataset/path` but scope allows only `/studies/**`

### Required categories of `normative_ast_checks`

#### A) API Contract Checks (Function Signatures)
Spec asserts:
- function name
- full signature (params + return type)
- ownership (layer_id)
- stability rules (breaking vs non-breaking)
- stable error codes returned/thrown

Enforcement:
- generated header: `generated/contracts/L{N}_{layer_id}_api.h`
- compile-time checks via `REGISTER_LAYER_API(...)`
- unit test verifies error-code registry contains declared codes

#### B) File Layout & Naming Checks (FileNames)
Spec asserts:
- required files exist (exact filenames)
- forbidden files (or forbidden includes) do not exist
- layer-local include boundaries (no cross-layer reach-through)
- generated artifacts are committed/available where required

Typical assertions:
- `docs/normative/L{N}_{layer_id}.md` exists and contains a valid contract block
- `contracts/L{N}_{layer_id}.manifest.json` exists and matches doc hash
- `src/layers/L{N}_{layer_id}/` exists
- `tests/layers/L{N}_{layer_id}/` exists
- fixture folders exist with minimum counts

Enforcement:
- repository-structure tests in CI (simple filesystem assertions)
- include-boundary lint (regex/AST include scan) to prevent cross-layer imports

#### C) Project Structure Checks (Module Boundaries)
Spec asserts:
- allowed dependencies for this layer (only lower layers or declared interfaces)
- public vs private surface (what headers are exposed)
- build targets and linkage expectations

Enforcement:
- `layers.lock.json` dependency policy checked in CI
- build system checks that targets do not link forbidden modules

#### D) Plan AST Shape Checks (CNF Node Requirements)
Spec asserts:
- required nodes exist when corresponding schema fields are present
- union/tag correctness for step types
- normalized defaults applied (CNF stability)
- reference resolution results (e.g., resolved universe symbol set is explicit in CNF)

Enforcement:
- structural AST unit tests generated from contract
- round-trip tests: `parse -> CNF -> serialize` stable

#### E) Diagnostic & Blame Pointer Checks
Spec asserts for each invalid fixture:
- exact error code(s)
- JSON Pointer blame paths
- layer attribution
- optional: DAG node IDs involved

Enforcement:
- test runner compares produced diagnostics to expected outputs

### Minimum enforcement requirements (per layer)
- At least **1** API signature check group (even for “data-only” layers)
- At least **1** file layout check group
- At least **1** plan AST shape check group
- Diagnostic checks for every invalid fixture

---


## AST Model (Canonical Normal Form)

### CNF Pipeline (mandatory)
1) Parse raw plan JSON
2) Apply defaults (schema defaults + canonicalization rules)
3) Resolve refs (including file-based universe if applicable)
4) Normalize into **CNF Plan AST** (stable ordering, explicit fields)
5) Semantic validation (L6)
6) Compile to runtime execution DAG (and lineage DAG)
7) Emit reproducible outputs + manifests

### Hashing
- `plan_cnf_hash = sha256(canonical_json)`
- `execution_dag_hash = sha256(canonical_dag)`
- `lineage_dag_hash = sha256(canonical_lineage)`

Hashes included in run manifest.

---

## DAGs (Do NOT conflate)

1) **Execution DAG**  
Order of operations & dependencies for runtime steps.
2) **Lineage DAG**  
Feature provenance, transformations, and data dependencies.
3) **ADR DAG**  
Decision history: why design changed, linked to schema pointers + contract IDs.

---

## Research-Integrity Tripwires (Layer L6)

These are semantic checks (not JSON Schema):

- **Same-bar leakage**: forbid edges where label/target uses data from same bar in feature inputs.
- **Staleness**: readiness must be monotonic; sentinel must include a cycle counter or timestamp-like counter, not just a constant 1.0.
- **Permutation drift**: any study input mutation must be declared via `parameters.permute` and recorded in run manifest.
- **Dedupe boundary**: session boundary must be part of dedupe keys when aggregating across sessions.
- **Plugin param mutation**: detect uncontrolled study input changes at runtime when in `bind` mode.
- **Gate correctness**: gates referenced must exist and must be evaluated before dependent steps.

Each rule:
- has stable error code `E_SEM_*`
- includes blame pointer(s) (JSON pointer) + optional DAG node IDs

---

## Testing Strategy (Per Layer)

Each layer ships two suites (generated primarily from `normative_ast_checks` in the contract block):

### A) Structural AST Suite
- schema pointer resolution tests
- parse → AST shape tests
- AST → canonical serialization round-trip tests
- layer boundary tests (no cross-layer reach)

### B) Semantic Invariant Suite
- invalid fixtures trigger exact `E_*` codes
- blame pointer matches expected JSON pointer
- optional: expected warning set matches

### Fixtures
- `fixtures/L{N}_{layer_id}/valid/*.json`
- `fixtures/L{N}_{layer_id}/invalid/*.json`

Minimum per layer: **10 valid + 10 invalid**, plus at least **3 “dangerous plausible” invalid** for L6.

---

## Compile-Time API Enforcement (C++)

Approach:
- Generate `generated/contracts/L{N}_{layer_id}_api.h` from contract manifest.
- Implementation must include header and register functions via macro:
  - `REGISTER_LAYER_API(layer_id, fn1, fn2, ...)`

If signatures drift, compilation fails.

---

## Runtime Architecture (ACSIL + Optional Tooling)

### Core Runtime Modules (C++)
- `src/core/` CNF parser, canonicalizer, error registry, logging
- `src/layers/L0_.../` … `src/layers/L8_.../` layer implementations
- `src/compile/` DAG compiler, lineage builder, run manifest emitter
- `src/sierra/` Sierra adapters (study binding, chart access, sentinel polling, permissions)

### Optional Developer Tooling (CLI)
- `tools/planlint/` validate + canonicalize + compile DAG offline
- `tools/contractgen/` extract contract blocks + generate manifests/tests/headers

> Tooling can be C++ or Python; runtime remains ACSIL-safe.

---

## Layer Boundary Enforcement (Hard Rule)

Layer boundaries are **enforced**, not suggested.

Authoritative policy:
- `schema/layers/layers.lock.json` defines allowed layer dependencies and allowed shared interfaces.
- Each layer’s contract block declares `dependencies.allowed_layer_ids` and `dependencies.allowed_interfaces` and **must** be a subset of `layers.lock.json`.

Enforcement:
- CI checks: dependency graph validity, include/link boundaries, and contract-deps ⊆ lock.
- Runtime guards: `ScopedPlanView` rejects out-of-scope JSON pointers; diagnostics validate error code ownership.


## Governance & Evolution

### Capability Negotiation
Plan header includes:
- `engine_capabilities_required: [...]`
- `engine_capabilities_optional: [...]`

Runtime publishes supported capabilities; missing required capabilities hard-fail with stable code.

### Deprecation Policy
- deprecate via capability flags + schema annotations
- support overlap window for 1–2 minor versions
- require ADR entry for any breaking change

### ADR DAG
- ADR nodes stored as small metadata entries:
  - id, parents, decision summary, affected pointers, contract_ids, doc refs
- ADR content lives in `docs/adr/ADR-XXXX.md`

---

## Milestones (Execution Order)

### Phase 0 — Toolchain Skeleton
- Create meta-contract schema (`contracts/meta.contract.schema.json`) and validate every Normative Contract Block against it
- Implement contract extraction + manifest generation
- Implement generated header + API registration macro

### Phase 1 — Layer L0 + CNF Baseline (Pilot)
- Split L0 schema
- Write normative doc + contract block
- Generate manifest + headers + tests
- Implement CNF parsing + round-trip canonicalization

### Phase 2 — L1 + L2 (Universe + Runtime Topology)
- Implement universe file resolution + hashing
- Implement readiness (sentinel) with monotonic counter requirement
- Add execution backend validation

### Phase 3 — L3 + L5 (Studies + Permutations)
- Study binding adapters (bind vs managed)
- Enforce permutation policies + record applied values
- Add “managed bridge” contract where applicable

### Phase 4 — L4 (Chains DSL)
- Define step interfaces + typed IO refs (preferred)
- Compile chains into execution DAG nodes

### Phase 5 — L6 (Semantics & Integrity)
- Implement tripwires + dangerous fixtures
- Add blame pointers + DAG node annotations

### Phase 6 — L7 + L8 (Outputs + Governance)
- Dataset emission + artifact manifest
- Capability negotiation + ADR DAG references
- Lock in error taxonomy and regression fixtures

---

## Definition of Done (Per Layer)

A layer is “done” only when:
- Layer schema composes cleanly into bundle
- Normative doc exists with validated contract block
- Contract manifest + generated headers/tests exist and are hashed in schema metadata
- Structural AST + Semantic invariants suites pass
- Layer boundary enforcement passes
- Stable error codes + blame pointers for all invalid fixtures
- ADR entry exists for any non-trivial design decision
- `normative_ast_checks` exist in the contract block and their generated tests pass (API, file layout, module boundaries, Plan AST shape)

---

## Appendices

### Error Taxonomy (Stable Prefixes)
- `E_SCHEMA_*` schema validation
- `E_CANON_*` canonicalization/ref resolution
- `E_SEM_*` semantic integrity
- `E_COMP_*` DAG compilation
- `E_RUN_*` runtime execution
- `E_IO_*` outputs/artifacts
- `E_LAYER_*` boundary/AST-check enforcement

### Recommended Sentinel Refinement (Avoid Staleness)
- Replace simple `ready_value == 1.0` with:
  - `ready_counter >= last_seen_counter + 1`
  - or include timestamp-like monotonically increasing value

---

## Next Action
- Implement Phase 0 + Phase 1 as the pilot and template the remaining layers mechanically.
