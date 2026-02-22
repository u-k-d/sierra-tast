#include "generated/contracts/L3_studies_features_api.h"
#include "sre/runtime.hpp"
#include "src/layers/common/layer_common.hpp"

namespace sre::layers {

Status ValidateStudiesFeatures(const ScopedPlanView& plan_view, DiagnosticSink& diag) noexcept {
  bool ok = true;
  auto emit = [&](std::string check_id, std::string reason, std::string expected, std::string actual, std::string pointer) {
    EmitLayerError(
        diag,
        "studies_features",
        "E_LAYER_STUDY_BINDING_INVALID",
        std::move(check_id),
        "CHKGRP_PLAN_AST_SHAPE",
        std::move(reason),
        std::move(expected),
        std::move(actual),
        {std::move(pointer)},
        "Fix studies block to satisfy mode/id/output requirements.",
        "contracts/L3_studies_features.manifest.json");
    ok = false;
  };

  const auto* studies = plan_view.Get("/studies");
  if (!studies || !studies->IsObject() || studies->AsObject().fields.empty()) {
    emit("CHK_STUDIES_OBJECT", "studies must be a non-empty object.", "object[minProperties=1]", TypeOf(studies), "/studies");
  } else {
    for (const auto& [study_key, spec] : studies->AsObject().fields) {
      const std::string base = "/studies/" + study_key;
      if (!spec.IsObject()) {
        emit("CHK_STUDY_SPEC_OBJECT", "study spec must be object.", "object", spec.TypeName(), base);
        continue;
      }
      const auto& s = spec.AsObject().fields;
      auto mode_it = s.find("mode");
      if (mode_it == s.end() || !mode_it->second.IsString() ||
          (mode_it->second.AsString() != "bind" && mode_it->second.AsString() != "managed")) {
        emit(
            "CHK_STUDY_MODE",
            "study mode must be bind or managed.",
            "bind|managed",
            mode_it == s.end() ? "missing" : mode_it->second.TypeName(),
            base + "/mode");
      }
      auto id_it = s.find("study_id");
      if (id_it == s.end() || !IsIntegerLike(id_it->second) || id_it->second.AsNumber() < 1.0) {
        emit(
            "CHK_STUDY_ID",
            "study_id must be integer >= 1.",
            ">=1 integer",
            id_it == s.end() ? "missing" : id_it->second.TypeName(),
            base + "/study_id");
      }
      auto out_it = s.find("outputs");
      if (out_it == s.end() || !out_it->second.IsObject() || out_it->second.AsObject().fields.empty()) {
        emit(
            "CHK_STUDY_OUTPUTS",
            "outputs must be non-empty object.",
            "object[minProperties=1]",
            out_it == s.end() ? "missing" : out_it->second.TypeName(),
            base + "/outputs");
      } else {
        for (const auto& [output_key, output_spec] : out_it->second.AsObject().fields) {
          const std::string out_base = base + "/outputs/" + output_key;
          if (!output_spec.IsObject()) {
            emit("CHK_STUDY_OUTPUT_SPEC", "output spec must be object.", "object", output_spec.TypeName(), out_base);
            continue;
          }
          const auto& o = output_spec.AsObject().fields;
          auto sub_it = o.find("subgraph");
          if (sub_it == o.end() || !IsIntegerLike(sub_it->second) || sub_it->second.AsNumber() < 0.0) {
            emit(
                "CHK_STUDY_OUTPUT_SUBGRAPH",
                "output subgraph must be integer >= 0.",
                ">=0 integer",
                sub_it == o.end() ? "missing" : sub_it->second.TypeName(),
                out_base + "/subgraph");
          }
        }
      }
    }
  }

  if (EmitFixtureMarkerViolation(
          plan_view,
          diag,
          "studies_features",
          "E_LAYER_STUDY_BINDING_INVALID",
          "/studies",
          "CHK_STUDIES_FIXTURE_MARKER")) {
    ok = false;
  }
  return ok ? Status::Success() : Status::Failure();
}

void RegisterStudiesFeaturesApi() noexcept { SRE_REGISTER_LAYER_API(studies_features, ValidateStudiesFeatures); }

}  // namespace sre::layers
