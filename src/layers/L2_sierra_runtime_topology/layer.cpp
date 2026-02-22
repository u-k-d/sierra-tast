#include "generated/contracts/L2_sierra_runtime_topology_api.h"
#include "sre/runtime.hpp"
#include "src/layers/common/layer_common.hpp"

namespace sre::layers {

Status ValidateSierraRuntimeTopology(const ScopedPlanView& plan_view, DiagnosticSink& diag) noexcept {
  bool ok = true;
  auto emit = [&](std::string check_id, std::string reason, std::string expected, std::string actual, std::string pointer) {
    EmitLayerError(
        diag,
        "sierra_runtime_topology",
        "E_LAYER_RUNTIME_TOPOLOGY_INVALID",
        std::move(check_id),
        "CHKGRP_PLAN_AST_SHAPE",
        std::move(reason),
        std::move(expected),
        std::move(actual),
        {std::move(pointer)},
        "Fix execution topology to satisfy Sierra runtime contract.",
        "contracts/L2_sierra_runtime_topology.manifest.json");
    ok = false;
  };

  const auto* execution = plan_view.Get("/execution");
  if (!execution || !execution->IsObject()) {
    emit("CHK_EXECUTION_OBJECT", "execution must be an object.", "object", TypeOf(execution), "/execution");
  } else {
    const auto& exec_fields = execution->AsObject().fields;

    auto worker_it = exec_fields.find("worker_charts");
    if (worker_it == exec_fields.end() || !worker_it->second.IsArray() || worker_it->second.AsArray().items.empty()) {
      emit(
          "CHK_WORKER_CHARTS_NONEMPTY",
          "execution.worker_charts must be a non-empty array.",
          "array[minItems=1]",
          worker_it == exec_fields.end() ? "missing" : worker_it->second.TypeName(),
          "/execution/worker_charts");
    } else {
      for (size_t i = 0; i < worker_it->second.AsArray().items.size(); ++i) {
        const auto& v = worker_it->second.AsArray().items[i];
        if (!IsIntegerLike(v) || v.AsNumber() < 1.0) {
          emit(
              "CHK_WORKER_CHART_VALUE",
              "worker chart numbers must be integers >= 1.",
              ">=1 integer",
              v.TypeName(),
              "/execution/worker_charts/" + std::to_string(i));
        }
      }
    }

    auto backend_it = exec_fields.find("backend");
    if (backend_it == exec_fields.end() || !backend_it->second.IsObject()) {
      emit(
          "CHK_BACKEND_OBJECT",
          "execution.backend must be an object.",
          "object",
          backend_it == exec_fields.end() ? "missing" : backend_it->second.TypeName(),
          "/execution/backend");
    } else {
      const auto& backend_fields = backend_it->second.AsObject().fields;
      auto type_it = backend_fields.find("type");
      if (type_it == backend_fields.end() || !type_it->second.IsString() || type_it->second.AsString() != "sierra_chart") {
        emit(
            "CHK_BACKEND_TYPE",
            "execution.backend.type must be sierra_chart.",
            "sierra_chart",
            type_it == backend_fields.end() ? "missing" : type_it->second.TypeName(),
            "/execution/backend/type");
      }
      auto sc_it = backend_fields.find("sierra_chart");
      if (sc_it == backend_fields.end() || !sc_it->second.IsObject()) {
        emit(
            "CHK_BACKEND_SIERRA_CHART",
            "execution.backend.sierra_chart must be present.",
            "object",
            sc_it == backend_fields.end() ? "missing" : sc_it->second.TypeName(),
            "/execution/backend/sierra_chart");
      } else {
        const auto& sc_fields = sc_it->second.AsObject().fields;
        auto layout_it = sc_fields.find("layout_contract");
        if (layout_it != sc_fields.end() && layout_it->second.IsObject()) {
          const auto& layout_fields = layout_it->second.AsObject().fields;
          auto readiness_it = layout_fields.find("readiness");
          if (readiness_it != layout_fields.end() && readiness_it->second.IsObject()) {
            const auto& r_fields = readiness_it->second.AsObject().fields;
            auto mode_it = r_fields.find("mode");
            if (mode_it != r_fields.end() && mode_it->second.IsString() && mode_it->second.AsString() == "gate") {
              auto ready_gate_it = r_fields.find("ready_gate");
              if (ready_gate_it == r_fields.end() || !ready_gate_it->second.IsString() || ready_gate_it->second.AsString().rfind("@gate.", 0) != 0) {
                emit(
                    "CHK_READINESS_GATE_REF",
                    "gate readiness mode requires ready_gate in @gate.<id> form.",
                    "@gate.<name>",
                    ready_gate_it == r_fields.end() ? "missing" : ready_gate_it->second.TypeName(),
                    "/execution/backend/sierra_chart/layout_contract/readiness/ready_gate");
              }
            }
          }
        }
      }
    }

    auto permissions_it = exec_fields.find("permissions");
    if (permissions_it != exec_fields.end() && !permissions_it->second.IsObject()) {
      emit("CHK_PERMISSIONS_OBJECT", "permissions must be an object when provided.", "object", permissions_it->second.TypeName(), "/execution/permissions");
    }
  }

  if (EmitFixtureMarkerViolation(
          plan_view,
          diag,
          "sierra_runtime_topology",
          "E_LAYER_RUNTIME_TOPOLOGY_INVALID",
          "/execution",
          "CHK_TOPOLOGY_FIXTURE_MARKER")) {
    ok = false;
  }
  return ok ? Status::Success() : Status::Failure();
}

void RegisterSierraRuntimeTopologyApi() noexcept {
  SRE_REGISTER_LAYER_API(sierra_runtime_topology, ValidateSierraRuntimeTopology);
}

}  // namespace sre::layers
