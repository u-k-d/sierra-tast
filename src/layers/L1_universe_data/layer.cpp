#include "generated/contracts/L1_universe_data_api.h"
#include "sre/runtime.hpp"
#include "src/layers/common/layer_common.hpp"

#include <regex>

namespace sre::layers {

Status ValidateUniverseData(const ScopedPlanView& plan_view, DiagnosticSink& diag) noexcept {
  bool ok = true;
  auto emit = [&](std::string check_id, std::string reason, std::string expected, std::string actual, std::string pointer) {
    EmitLayerError(
        diag,
        "universe_data",
        "E_LAYER_UNIVERSE_INVALID",
        std::move(check_id),
        "CHKGRP_PLAN_AST_SHAPE",
        std::move(reason),
        std::move(expected),
        std::move(actual),
        {std::move(pointer)},
        "Fix universe.source specific fields and date/timeframe semantics.",
        "contracts/L1_universe_data.manifest.json");
    ok = false;
  };

  const auto* universe = plan_view.Get("/universe");
  if (!universe || !universe->IsObject()) {
    emit("CHK_UNIVERSE_OBJECT", "universe must be an object.", "object", universe ? universe->TypeName() : "missing", "/universe");
  } else {
    const auto& fields = universe->AsObject().fields;
    auto source_it = fields.find("source");
    if (source_it == fields.end() || !source_it->second.IsString()) {
      emit("CHK_UNIVERSE_SOURCE", "universe.source is required.", "inline|file", source_it == fields.end() ? "missing" : source_it->second.TypeName(), "/universe/source");
    } else {
      const std::string source = source_it->second.AsString();
      if (source != "inline" && source != "file") {
        emit("CHK_UNIVERSE_SOURCE_ENUM", "universe.source must be inline or file.", "inline|file", source, "/universe/source");
      }
      if (source == "inline") {
        auto symbols_it = fields.find("symbols");
        if (symbols_it == fields.end() || !symbols_it->second.IsArray() || symbols_it->second.AsArray().items.empty()) {
          emit("CHK_UNIVERSE_INLINE_SYMBOLS", "inline source requires non-empty symbols array.", "array[minItems=1]", symbols_it == fields.end() ? "missing" : symbols_it->second.TypeName(), "/universe/symbols");
        }
      }
      if (source == "file") {
        auto file_it = fields.find("file");
        if (file_it == fields.end() || !file_it->second.IsObject()) {
          emit("CHK_UNIVERSE_FILE_OBJECT", "file source requires universe.file object.", "object", file_it == fields.end() ? "missing" : file_it->second.TypeName(), "/universe/file");
        }
      }
    }

    auto timeframe_it = fields.find("timeframe");
    if (timeframe_it != fields.end()) {
      const std::regex re("^[0-9]+(s|m|h|d|w)$");
      if (!timeframe_it->second.IsString() || !std::regex_match(timeframe_it->second.AsString(), re)) {
        emit("CHK_UNIVERSE_TIMEFRAME", "timeframe must match ^[0-9]+(s|m|h|d|w)$.", "e.g. 5m", timeframe_it->second.TypeName(), "/universe/timeframe");
      }
    }

    auto date_range_it = fields.find("date_range");
    if (date_range_it != fields.end()) {
      const std::regex date_re("^\\d{4}-\\d{2}-\\d{2}$");
      if (!date_range_it->second.IsObject()) {
        emit("CHK_UNIVERSE_DATE_RANGE", "date_range must be object.", "object", date_range_it->second.TypeName(), "/universe/date_range");
      } else {
        const auto& dr = date_range_it->second.AsObject().fields;
        auto s_it = dr.find("start");
        auto e_it = dr.find("end");
        if (s_it == dr.end() || !s_it->second.IsString() || !std::regex_match(s_it->second.AsString(), date_re)) {
          emit("CHK_UNIVERSE_DATE_START", "date_range.start must be YYYY-MM-DD.", "YYYY-MM-DD", s_it == dr.end() ? "missing" : s_it->second.TypeName(), "/universe/date_range/start");
        }
        if (e_it == dr.end() || !e_it->second.IsString() || !std::regex_match(e_it->second.AsString(), date_re)) {
          emit("CHK_UNIVERSE_DATE_END", "date_range.end must be YYYY-MM-DD.", "YYYY-MM-DD", e_it == dr.end() ? "missing" : e_it->second.TypeName(), "/universe/date_range/end");
        }
      }
    }
  }

  if (EmitFixtureMarkerViolation(
          plan_view,
          diag,
          "universe_data",
          "E_LAYER_UNIVERSE_INVALID",
          "/universe",
          "CHK_DIAG_FIXTURE_MARKER")) {
    ok = false;
  }

  return ok ? Status::Success() : Status::Failure();
}

void RegisterUniverseDataApi() noexcept { SRE_REGISTER_LAYER_API(universe_data, ValidateUniverseData); }

}  // namespace sre::layers
