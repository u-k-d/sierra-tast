#include "generated/contracts/L0_core_identity_io_api.h"
#include "sre/runtime.hpp"
#include "src/layers/common/layer_common.hpp"

namespace sre::layers {

Status ValidateCoreIdentityIo(const ScopedPlanView& plan_view, DiagnosticSink& diag) noexcept {
  bool ok = true;
  auto emit = [&](std::string check_id, std::string reason, std::string expected, std::string actual, std::string pointer) {
    EmitLayerError(
        diag,
        "core_identity_io",
        "E_LAYER_CORE_IDENTITY_INVALID",
        std::move(check_id),
        "CHKGRP_PLAN_AST_SHAPE",
        std::move(reason),
        std::move(expected),
        std::move(actual),
        {std::move(pointer)},
        "Fix plan header fields to match Sierra Runtime v2.6.2 contract.",
        "contracts/L0_core_identity_io.manifest.json");
    ok = false;
  };

  const auto* version = plan_view.Get("/version");
  if (!version || !version->IsNumber() || static_cast<int>(version->AsNumber()) != 262) {
    emit("CHK_VERSION_CONST", "Plan version must be exactly 262.", "262", version ? version->TypeName() : "missing", "/version");
  }

  const auto* plan_kind = plan_view.Get("/plan_kind");
  if (!plan_kind || !plan_kind->IsString() || plan_kind->AsString() != "sierra_research_engine.plan") {
    emit(
        "CHK_PLAN_KIND_CONST",
        "plan_kind must identify Sierra Research Engine plan.",
        "sierra_research_engine.plan",
        plan_kind ? plan_kind->TypeName() : "missing",
        "/plan_kind");
  }

  const auto* plan_io = plan_view.Get("/plan_io");
  if (!plan_io || !plan_io->IsObject()) {
    emit("CHK_PLAN_IO_OBJECT", "plan_io must be an object.", "object", plan_io ? plan_io->TypeName() : "missing", "/plan_io");
  } else {
    const auto& fields = plan_io->AsObject().fields;
    auto format_it = fields.find("format");
    if (format_it == fields.end() || !format_it->second.IsString() || format_it->second.AsString() != "json") {
      emit("CHK_PLAN_IO_FORMAT", "plan_io.format must be json.", "json", format_it == fields.end() ? "missing" : format_it->second.TypeName(), "/plan_io/format");
    }
    auto encoding_it = fields.find("encoding");
    if (encoding_it != fields.end() && (!encoding_it->second.IsString() || encoding_it->second.AsString() != "utf-8")) {
      emit("CHK_PLAN_IO_ENCODING", "plan_io.encoding must be utf-8 when specified.", "utf-8", encoding_it->second.TypeName(), "/plan_io/encoding");
    }
    auto allow_comments_it = fields.find("allow_comments");
    if (allow_comments_it != fields.end() && !allow_comments_it->second.IsBool()) {
      emit(
          "CHK_PLAN_IO_ALLOW_COMMENTS",
          "plan_io.allow_comments must be boolean.",
          "bool",
          allow_comments_it->second.TypeName(),
          "/plan_io/allow_comments");
    }
    auto max_bytes_it = fields.find("max_bytes");
    if (max_bytes_it != fields.end()) {
      if (!max_bytes_it->second.IsNumber() || max_bytes_it->second.AsNumber() < 1024.0) {
        emit("CHK_PLAN_IO_MAX_BYTES", "plan_io.max_bytes must be >= 1024.", ">=1024", max_bytes_it->second.TypeName(), "/plan_io/max_bytes");
      }
    }
  }

  if (EmitFixtureMarkerViolation(
          plan_view,
          diag,
          "core_identity_io",
          "E_LAYER_CORE_IDENTITY_INVALID",
          "/version",
          "CHK_DIAG_FIXTURE_MARKER")) {
    ok = false;
  }
  return ok ? Status::Success() : Status::Failure();
}

void RegisterCoreIdentityIoApi() noexcept { SRE_REGISTER_LAYER_API(core_identity_io, ValidateCoreIdentityIo); }

}  // namespace sre::layers
