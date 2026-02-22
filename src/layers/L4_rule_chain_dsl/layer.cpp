#include "generated/contracts/L4_rule_chain_dsl_api.h"
#include "sre/runtime.hpp"
#include "src/layers/common/layer_common.hpp"

#include <cctype>
#include <set>

namespace sre::layers {

Status ValidateRuleChainDsl(const ScopedPlanView& plan_view, DiagnosticSink& diag) noexcept {
  bool ok = true;
  auto emit = [&](std::string check_id, std::string reason, std::string expected, std::string actual, std::string pointer) {
    EmitLayerError(
        diag,
        "rule_chain_dsl",
        "E_LAYER_CHAIN_DSL_INVALID",
        std::move(check_id),
        "CHKGRP_PLAN_AST_SHAPE",
        std::move(reason),
        std::move(expected),
        std::move(actual),
        {std::move(pointer)},
        "Fix chains/gates/signal references for valid DSL graph.",
        "contracts/L4_rule_chain_dsl.manifest.json");
    ok = false;
  };

  const auto* chains = plan_view.Get("/chains");
  bool require_gates_exist = true;
  const auto* require_gates = plan_view.Get("/validation/require_gates_exist");
  if (require_gates && require_gates->IsBool()) {
    require_gates_exist = require_gates->AsBool();
  }

  bool prefer_typed_refs = true;
  const auto* prefer_typed = plan_view.Get("/validation/prefer_typed_step_io_refs");
  if (prefer_typed && prefer_typed->IsBool()) {
    prefer_typed_refs = prefer_typed->AsBool();
  }

  std::set<std::string> gate_ids;
  const auto* gates = plan_view.Get("/gates");
  if (gates && gates->IsObject()) {
    for (const auto& [k, _] : gates->AsObject().fields) {
      gate_ids.insert(k);
    }
  }

  const auto* signals = plan_view.Get("/signals");
  if (signals && !signals->IsObject()) {
    emit("CHK_SIGNALS_OBJECT", "signals must be object when provided.", "object", signals->TypeName(), "/signals");
  }
  if (gates && !gates->IsObject()) {
    emit("CHK_GATES_OBJECT", "gates must be object when provided.", "object", gates->TypeName(), "/gates");
  }

  if (!chains || !chains->IsObject() || chains->AsObject().fields.empty()) {
    emit("CHK_CHAINS_OBJECT", "chains must be a non-empty object.", "object[minProperties=1]", TypeOf(chains), "/chains");
  } else {
    for (const auto& [chain_id, chain_spec] : chains->AsObject().fields) {
      const std::string base = "/chains/" + chain_id;
      if (!chain_spec.IsObject()) {
        emit("CHK_CHAIN_SPEC_OBJECT", "chain spec must be object.", "object", chain_spec.TypeName(), base);
        continue;
      }
      const auto& c = chain_spec.AsObject().fields;
      auto steps_it = c.find("steps");
      if (steps_it == c.end() || !steps_it->second.IsArray() || steps_it->second.AsArray().items.empty()) {
        emit(
            "CHK_CHAIN_STEPS",
            "chain steps must be non-empty array.",
            "array[minItems=1]",
            steps_it == c.end() ? "missing" : steps_it->second.TypeName(),
            base + "/steps");
        continue;
      }
      std::set<std::string> produced_step_aliases;
      for (size_t i = 0; i < steps_it->second.AsArray().items.size(); ++i) {
        const auto& step = steps_it->second.AsArray().items[i];
        const std::string step_ptr = base + "/steps/" + std::to_string(i);
        if (!step.IsObject()) {
          emit("CHK_STEP_OBJECT", "step must be object.", "object", step.TypeName(), step_ptr);
          continue;
        }
        const auto& sf = step.AsObject().fields;
        auto kind_it = sf.find("kind");
        if (kind_it == sf.end() || !kind_it->second.IsString()) {
          emit("CHK_STEP_KIND", "step.kind is required and must be string.", "string", kind_it == sf.end() ? "missing" : kind_it->second.TypeName(), step_ptr + "/kind");
        }

        for (const auto& [field, value] : sf) {
          if (!value.IsString()) {
            continue;
          }
          const std::string v = value.AsString();

          if (prefer_typed_refs && (field == "from" || field == "source" || field == "left" || field == "right")) {
            if (!v.empty() && v[0] != '@') {
              emit(
                  "CHK_CHAIN_TYPED_REF",
                  "step IO ref should use typed @ref notation when prefer_typed_step_io_refs=true.",
                  "@study.*|@step.*|@signal.*|@gate.*",
                  v,
                  step_ptr + "/" + field);
            }
          }

          std::string gate_prefix = "@gate.";
          size_t gpos = v.find(gate_prefix);
          while (gpos != std::string::npos) {
            size_t start = gpos + gate_prefix.size();
            size_t end = start;
            while (end < v.size() && (std::isalnum(static_cast<unsigned char>(v[end])) || v[end] == '_')) {
              ++end;
            }
            const std::string gate_id = v.substr(start, end - start);
            if (require_gates_exist && !gate_id.empty() && !gate_ids.contains(gate_id)) {
              emit(
                  "CHK_CHAIN_GATE_EXISTS",
                  "Referenced gate does not exist.",
                  "existing gate id",
                  gate_id,
                  "/gates/" + gate_id);
            }
            gpos = v.find(gate_prefix, end);
          }

          std::string step_prefix = "@step.";
          size_t spos = v.find(step_prefix);
          while (spos != std::string::npos) {
            size_t start = spos + step_prefix.size();
            size_t dot = v.find('.', start);
            if (dot != std::string::npos) {
              std::string ref_chain = v.substr(start, dot - start);
              size_t end = dot + 1;
              while (end < v.size() && (std::isalnum(static_cast<unsigned char>(v[end])) || v[end] == '_')) {
                ++end;
              }
              std::string ref_alias = v.substr(dot + 1, end - (dot + 1));
              if (ref_chain == chain_id && !ref_alias.empty() && !produced_step_aliases.contains(ref_alias)) {
                emit(
                    "CHK_CHAIN_STEP_DEP_ORDER",
                    "step reference must resolve to a previously produced alias in same chain.",
                    "reference to previously defined step alias",
                    ref_alias,
                    step_ptr + "/" + field);
              }
              spos = v.find(step_prefix, end);
            } else {
              break;
            }
          }
        }

        auto as_it = sf.find("as");
        if (as_it != sf.end() && as_it->second.IsString() && !as_it->second.AsString().empty()) {
          produced_step_aliases.insert(as_it->second.AsString());
        }
      }
    }
  }

  const auto* execution_mode = plan_view.Get("/execution/backend/sierra_chart/layout_contract/readiness/mode");
  if (execution_mode && execution_mode->IsString() && execution_mode->AsString() == "gate") {
    const auto* ready_gate = plan_view.Get("/execution/backend/sierra_chart/layout_contract/readiness/ready_gate");
    if (!ready_gate || !ready_gate->IsString() || ready_gate->AsString().rfind("@gate.", 0) != 0) {
      emit("CHK_READY_GATE_FORMAT", "ready_gate must be @gate.<id> when readiness.mode=gate.", "@gate.<id>", TypeOf(ready_gate), "/execution/backend/sierra_chart/layout_contract/readiness/ready_gate");
    } else {
      const std::string gate_id = ready_gate->AsString().substr(6);
      if (!plan_view.Exists("/gates/" + gate_id)) {
        emit("CHK_READY_GATE_EXISTS", "ready_gate reference must exist in /gates.", "existing gate id", gate_id, "/gates/" + gate_id);
      }
    }
  }

  if (EmitFixtureMarkerViolation(
          plan_view,
          diag,
          "rule_chain_dsl",
          "E_LAYER_CHAIN_DSL_INVALID",
          "/chains",
          "CHK_RULE_CHAIN_FIXTURE_MARKER")) {
    ok = false;
  }
  return ok ? Status::Success() : Status::Failure();
}

void RegisterRuleChainDslApi() noexcept { SRE_REGISTER_LAYER_API(rule_chain_dsl, ValidateRuleChainDsl); }

}  // namespace sre::layers
