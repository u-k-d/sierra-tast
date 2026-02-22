#include "generated/contracts/L4_rule_chain_dsl_api.h"
#include "sre/runtime.hpp"
#include "src/layers/common/layer_common.hpp"

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
