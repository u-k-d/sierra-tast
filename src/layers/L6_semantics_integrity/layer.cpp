#include "generated/contracts/L6_semantics_integrity_api.h"
#include "sre/runtime.hpp"
#include "src/layers/common/layer_common.hpp"

namespace sre::layers {

Status ValidateSemanticsIntegrity(const ScopedPlanView& plan_view, DiagnosticSink& diag) noexcept {
  bool ok = true;
  auto emit = [&](std::string check_id, std::string reason, std::string expected, std::string actual, std::string pointer) {
    EmitLayerError(
        diag,
        "semantics_integrity",
        "E_SEM_INTEGRITY_RULE_FAILED",
        std::move(check_id),
        "CHKGRP_PLAN_AST_SHAPE",
        std::move(reason),
        std::move(expected),
        std::move(actual),
        {std::move(pointer)},
        "Fix semantic integrity invariant violation.",
        "contracts/L6_semantics_integrity.manifest.json");
    ok = false;
  };

  const auto* require_readiness = plan_view.Get("/validation/require_sierra_readiness_contract");
  if (require_readiness && require_readiness->IsBool() && require_readiness->AsBool()) {
    const bool sentinel_enabled =
        plan_view.Exists("/execution/sentinel/enabled") &&
        plan_view.Get("/execution/sentinel/enabled")->IsBool() &&
        plan_view.Get("/execution/sentinel/enabled")->AsBool();
    const bool gate_mode =
        plan_view.Exists("/execution/backend/sierra_chart/layout_contract/readiness/mode") &&
        plan_view.Get("/execution/backend/sierra_chart/layout_contract/readiness/mode")->IsString() &&
        plan_view.Get("/execution/backend/sierra_chart/layout_contract/readiness/mode")->AsString() == "gate";
    if (!sentinel_enabled && !gate_mode) {
      emit(
          "CHK_READINESS_CONTRACT",
          "readiness contract required but neither sentinel nor gate readiness configured.",
          "sentinel.enabled=true or readiness.mode=gate",
          "none",
          "/execution");
    }
  }

  // Same-bar leakage tripwire placeholder: detect explicit same_bar source markers.
  const auto* chains = plan_view.Get("/chains");
  if (chains && chains->IsObject()) {
    for (const auto& [chain_id, chain_spec] : chains->AsObject().fields) {
      if (!chain_spec.IsObject()) {
        continue;
      }
      auto steps_it = chain_spec.AsObject().fields.find("steps");
      if (steps_it == chain_spec.AsObject().fields.end() || !steps_it->second.IsArray()) {
        continue;
      }
      for (size_t i = 0; i < steps_it->second.AsArray().items.size(); ++i) {
        const auto& step = steps_it->second.AsArray().items[i];
        if (!step.IsObject()) {
          continue;
        }
        auto from_it = step.AsObject().fields.find("from");
        if (from_it != step.AsObject().fields.end() && from_it->second.IsString() &&
            from_it->second.AsString().find("same_bar") != std::string::npos) {
          emit(
              "CHK_SEM_SAME_BAR_LEAKAGE",
              "same_bar marker found in step source; potential leakage.",
              "no same_bar source marker",
              from_it->second.AsString(),
              "/chains/" + chain_id + "/steps/" + std::to_string(i) + "/from");
        }
      }
    }
  }

  if (EmitFixtureMarkerViolation(
          plan_view,
          diag,
          "semantics_integrity",
          "E_SEM_INTEGRITY_RULE_FAILED",
          "/validation",
          "CHK_SEMANTICS_FIXTURE_MARKER")) {
    ok = false;
  }
  return ok ? Status::Success() : Status::Failure();
}

void RegisterSemanticsIntegrityApi() noexcept { SRE_REGISTER_LAYER_API(semantics_integrity, ValidateSemanticsIntegrity); }

}  // namespace sre::layers
