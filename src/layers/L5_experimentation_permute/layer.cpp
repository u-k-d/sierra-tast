#include "generated/contracts/L5_experimentation_permute_api.h"
#include "sre/runtime.hpp"
#include "src/layers/common/layer_common.hpp"

namespace sre::layers {

Status ValidateExperimentationPermute(const ScopedPlanView& plan_view, DiagnosticSink& diag) noexcept {
  bool ok = true;
  auto emit = [&](std::string check_id, std::string reason, std::string expected, std::string actual, std::string pointer) {
    EmitLayerError(
        diag,
        "experimentation_permute",
        "E_LAYER_PERMUTE_INVALID",
        std::move(check_id),
        "CHKGRP_PLAN_AST_SHAPE",
        std::move(reason),
        std::move(expected),
        std::move(actual),
        {std::move(pointer)},
        "Fix parameters.permute item shape and kind-specific fields.",
        "contracts/L5_experimentation_permute.manifest.json");
    ok = false;
  };

  const auto* params = plan_view.Get("/parameters");
  if (params && !params->IsObject()) {
    emit("CHK_PARAMETERS_OBJECT", "parameters must be object when present.", "object", params->TypeName(), "/parameters");
  } else if (params && params->IsObject()) {
    const auto& p = params->AsObject().fields;
    auto perm_it = p.find("permute");
    if (perm_it != p.end()) {
      if (!perm_it->second.IsArray()) {
        emit("CHK_PERMUTE_ARRAY", "parameters.permute must be array.", "array", perm_it->second.TypeName(), "/parameters/permute");
      } else {
        for (size_t i = 0; i < perm_it->second.AsArray().items.size(); ++i) {
          const auto& item = perm_it->second.AsArray().items[i];
          const std::string base = "/parameters/permute/" + std::to_string(i);
          if (!item.IsObject()) {
            emit("CHK_PERMUTE_ITEM_OBJECT", "permute item must be object.", "object", item.TypeName(), base);
            continue;
          }
          const auto& it = item.AsObject().fields;
          auto id_it = it.find("id");
          auto kind_it = it.find("kind");
          auto values_it = it.find("values");
          if (id_it == it.end() || !id_it->second.IsString() || id_it->second.AsString().empty()) {
            emit("CHK_PERMUTE_ID", "permute.id is required and must be non-empty string.", "string", id_it == it.end() ? "missing" : id_it->second.TypeName(), base + "/id");
          }
          if (kind_it == it.end() || !kind_it->second.IsString()) {
            emit("CHK_PERMUTE_KIND", "permute.kind is required.", "study_input|rule_param|symbol", kind_it == it.end() ? "missing" : kind_it->second.TypeName(), base + "/kind");
            continue;
          }
          const std::string kind = kind_it->second.AsString();
          if (kind != "study_input" && kind != "rule_param" && kind != "symbol") {
            emit("CHK_PERMUTE_KIND_ENUM", "permute.kind is invalid.", "study_input|rule_param|symbol", kind, base + "/kind");
          }
          if (values_it == it.end() || !values_it->second.IsArray() || values_it->second.AsArray().items.empty()) {
            emit("CHK_PERMUTE_VALUES", "permute.values must be non-empty array.", "array[minItems=1]", values_it == it.end() ? "missing" : values_it->second.TypeName(), base + "/values");
          }
          if (kind == "study_input") {
            auto study_it = it.find("study");
            auto input_it = it.find("input");
            if (study_it == it.end() || !study_it->second.IsString() || study_it->second.AsString().empty()) {
              emit("CHK_PERMUTE_STUDY_INPUT_STUDY", "study_input permutation requires study.", "string", study_it == it.end() ? "missing" : study_it->second.TypeName(), base + "/study");
            }
            if (input_it == it.end() || !input_it->second.IsString() || input_it->second.AsString().empty()) {
              emit("CHK_PERMUTE_STUDY_INPUT_INPUT", "study_input permutation requires input.", "string", input_it == it.end() ? "missing" : input_it->second.TypeName(), base + "/input");
            }
          }
        }
      }
    }
  }

  if (EmitFixtureMarkerViolation(
          plan_view,
          diag,
          "experimentation_permute",
          "E_LAYER_PERMUTE_INVALID",
          "/parameters",
          "CHK_PERMUTE_FIXTURE_MARKER")) {
    ok = false;
  }
  return ok ? Status::Success() : Status::Failure();
}

void RegisterExperimentationPermuteApi() noexcept {
  SRE_REGISTER_LAYER_API(experimentation_permute, ValidateExperimentationPermute);
}

}  // namespace sre::layers
