#include "generated/contracts/L8_governance_evolution_api.h"
#include "sre/runtime.hpp"
#include "src/layers/common/layer_common.hpp"

namespace sre::layers {

Status ValidateGovernanceEvolution(const ScopedPlanView& plan_view, DiagnosticSink& diag) noexcept {
  bool ok = true;
  auto emit = [&](std::string check_id, std::string reason, std::string expected, std::string actual, std::string pointer) {
    EmitLayerError(
        diag,
        "governance_evolution",
        "E_LAYER_GOVERNANCE_INVALID",
        std::move(check_id),
        "CHKGRP_PLAN_AST_SHAPE",
        std::move(reason),
        std::move(expected),
        std::move(actual),
        {std::move(pointer)},
        "Fix governance capability or ADR DAG metadata.",
        "contracts/L8_governance_evolution.manifest.json");
    ok = false;
  };

  const auto* dag = plan_view.Get("/dag");
  if (dag && !dag->IsNull() && !dag->IsObject()) {
    emit("CHK_DAG_TYPE", "dag must be object or null.", "object|null", dag->TypeName(), "/dag");
  }

  const auto* required_caps = plan_view.Get("/meta/engine_capabilities_required");
  if (required_caps) {
    if (!required_caps->IsArray()) {
      emit("CHK_REQUIRED_CAPS_ARRAY", "engine_capabilities_required must be array.", "array", required_caps->TypeName(), "/meta/engine_capabilities_required");
    } else {
      for (size_t i = 0; i < required_caps->AsArray().items.size(); ++i) {
        const auto& item = required_caps->AsArray().items[i];
        if (!item.IsString() || item.AsString().empty()) {
          emit(
              "CHK_REQUIRED_CAP_ITEM",
              "required capability ids must be non-empty strings.",
              "non-empty string",
              item.TypeName(),
              "/meta/engine_capabilities_required/" + std::to_string(i));
        }
      }
    }
  }

  const auto* optional_caps = plan_view.Get("/meta/engine_capabilities_optional");
  if (optional_caps && !optional_caps->IsArray()) {
    emit("CHK_OPTIONAL_CAPS_ARRAY", "engine_capabilities_optional must be array.", "array", optional_caps->TypeName(), "/meta/engine_capabilities_optional");
  }

  if (EmitFixtureMarkerViolation(
          plan_view,
          diag,
          "governance_evolution",
          "E_LAYER_GOVERNANCE_INVALID",
          "/dag",
          "CHK_GOVERNANCE_FIXTURE_MARKER")) {
    ok = false;
  }
  return ok ? Status::Success() : Status::Failure();
}

void RegisterGovernanceEvolutionApi() noexcept { SRE_REGISTER_LAYER_API(governance_evolution, ValidateGovernanceEvolution); }

}  // namespace sre::layers
