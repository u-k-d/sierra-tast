#include "generated/contracts/L7_outputs_repro_api.h"
#include "sre/runtime.hpp"
#include "src/layers/common/layer_common.hpp"

namespace sre::layers {

Status ValidateOutputsRepro(const ScopedPlanView& plan_view, DiagnosticSink& diag) noexcept {
  bool ok = true;
  auto emit = [&](std::string check_id, std::string reason, std::string expected, std::string actual, std::string pointer) {
    EmitLayerError(
        diag,
        "outputs_repro",
        "E_IO_OUTPUT_CONFIG_INVALID",
        std::move(check_id),
        "CHKGRP_PLAN_AST_SHAPE",
        std::move(reason),
        std::move(expected),
        std::move(actual),
        {std::move(pointer)},
        "Fix outputs dataset/artifact configuration.",
        "contracts/L7_outputs_repro.manifest.json");
    ok = false;
  };

  const auto* outputs = plan_view.Get("/outputs");
  bool allow_fs_write = false;
  bool fs_permission_explicit = false;
  const auto* permissions = plan_view.Get("/execution/permissions");
  if (permissions && permissions->IsObject()) {
    fs_permission_explicit = true;
  }
  const auto* allow_fs = plan_view.Get("/execution/permissions/allow_filesystem_write");
  if (allow_fs && allow_fs->IsBool()) {
    allow_fs_write = allow_fs->AsBool();
    fs_permission_explicit = true;
  }

  if (!outputs || !outputs->IsObject()) {
    emit("CHK_OUTPUTS_OBJECT", "outputs must be an object.", "object", TypeOf(outputs), "/outputs");
  } else {
    const auto& o = outputs->AsObject().fields;
    auto dataset_it = o.find("dataset");
    if (dataset_it == o.end() || !dataset_it->second.IsObject()) {
      emit("CHK_DATASET_OBJECT", "outputs.dataset is required.", "object", dataset_it == o.end() ? "missing" : dataset_it->second.TypeName(), "/outputs/dataset");
    } else {
      const auto& d = dataset_it->second.AsObject().fields;
      auto format_it = d.find("format");
      if (format_it == d.end() || !format_it->second.IsString() ||
          (format_it->second.AsString() != "csv" && format_it->second.AsString() != "jsonl")) {
        emit("CHK_DATASET_FORMAT", "dataset.format must be csv or jsonl.", "csv|jsonl", format_it == d.end() ? "missing" : format_it->second.TypeName(), "/outputs/dataset/format");
      }
      auto path_it = d.find("path");
      if (path_it == d.end() || !path_it->second.IsString() || path_it->second.AsString().empty()) {
        emit("CHK_DATASET_PATH", "dataset.path must be non-empty string.", "non-empty string", path_it == d.end() ? "missing" : path_it->second.TypeName(), "/outputs/dataset/path");
      } else if (fs_permission_explicit && !allow_fs_write) {
        emit(
            "CHK_OUTPUT_FILESYSTEM_PERMISSION",
            "dataset output path requires execution.permissions.allow_filesystem_write=true.",
            "allow_filesystem_write=true",
            "false",
            "/execution/permissions/allow_filesystem_write");
      }
      auto fields_it = d.find("fields");
      if (fields_it == d.end() || !fields_it->second.IsArray() || fields_it->second.AsArray().items.empty()) {
        emit("CHK_DATASET_FIELDS", "dataset.fields must be non-empty array.", "array[minItems=1]", fields_it == d.end() ? "missing" : fields_it->second.TypeName(), "/outputs/dataset/fields");
      }
    }

    auto artifacts_it = o.find("artifacts");
    if (artifacts_it != o.end()) {
      if (!artifacts_it->second.IsObject()) {
        emit("CHK_ARTIFACTS_OBJECT", "outputs.artifacts must be object.", "object", artifacts_it->second.TypeName(), "/outputs/artifacts");
      } else {
        bool artifacts_enabled = true;
        const auto& a = artifacts_it->second.AsObject().fields;
        auto enabled_it = a.find("enabled");
        if (enabled_it != a.end()) {
          if (!enabled_it->second.IsBool()) {
            emit("CHK_ARTIFACTS_ENABLED", "artifacts.enabled must be bool.", "bool", enabled_it->second.TypeName(), "/outputs/artifacts/enabled");
          } else {
            artifacts_enabled = enabled_it->second.AsBool();
          }
        }
        auto base_dir_it = a.find("base_dir");
        if (base_dir_it != a.end() && (!base_dir_it->second.IsString() || base_dir_it->second.AsString().empty())) {
          emit("CHK_ARTIFACTS_BASE_DIR", "artifacts.base_dir must be non-empty string.", "non-empty string", base_dir_it->second.TypeName(), "/outputs/artifacts/base_dir");
        }
        auto manifest_it = a.find("write_run_manifest");
        if (manifest_it != a.end() && !manifest_it->second.IsBool()) {
          emit("CHK_ARTIFACTS_WRITE_MANIFEST", "artifacts.write_run_manifest must be bool.", "bool", manifest_it->second.TypeName(), "/outputs/artifacts/write_run_manifest");
        }
        auto metrics_it = a.find("write_metrics_summary");
        if (metrics_it != a.end() && !metrics_it->second.IsBool()) {
          emit("CHK_ARTIFACTS_WRITE_METRICS", "artifacts.write_metrics_summary must be bool.", "bool", metrics_it->second.TypeName(), "/outputs/artifacts/write_metrics_summary");
        }
        if (artifacts_enabled && fs_permission_explicit && !allow_fs_write) {
          emit(
              "CHK_ARTIFACTS_FILESYSTEM_PERMISSION",
              "artifacts emission requires execution.permissions.allow_filesystem_write=true.",
              "allow_filesystem_write=true",
              "false",
              "/execution/permissions/allow_filesystem_write");
        }
      }
    }
  }

  if (EmitFixtureMarkerViolation(
          plan_view,
          diag,
          "outputs_repro",
          "E_IO_OUTPUT_CONFIG_INVALID",
          "/outputs",
          "CHK_OUTPUTS_FIXTURE_MARKER")) {
    ok = false;
  }
  return ok ? Status::Success() : Status::Failure();
}

void RegisterOutputsReproApi() noexcept { SRE_REGISTER_LAYER_API(outputs_repro, ValidateOutputsRepro); }

}  // namespace sre::layers
