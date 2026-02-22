#include "generated/contracts/L6_semantics_integrity_api.h"
#include "sre/runtime.hpp"
#include "src/layers/common/layer_common.hpp"

#include <cctype>
#include <set>

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
  auto emit_policy = [&](std::string check_id, std::string reason, std::string expected, std::string actual, std::string pointer, std::string on_violation) {
    if (on_violation == "error") {
      emit(std::move(check_id), std::move(reason), std::move(expected), std::move(actual), std::move(pointer));
      return;
    }
    Diagnostic d;
    d.code = "E_SEM_INTEGRITY_RULE_FAILED";
    d.layer_id = "semantics_integrity";
    d.check_id = std::move(check_id);
    d.group_id = "CHKGRP_PLAN_AST_SHAPE";
    d.reason = std::move(reason);
    d.expected = std::move(expected);
    d.actual = std::move(actual);
    d.blame_pointers = {std::move(pointer)};
    d.remediation = on_violation == "skip_permutation" ? "Skip violating permutation and continue with policy-compliant set."
                                                        : "Adjust permutation policy or study mode.";
    d.severity = "warn";
    d.source = "contracts/L6_semantics_integrity.manifest.json";
    diag.Emit(d);
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

  // Staleness tripwire: if sentinel mode is used, constant ready_value=1.0 is too weak.
  if (plan_view.Exists("/execution/sentinel/enabled") && plan_view.Get("/execution/sentinel/enabled")->IsBool() &&
      plan_view.Get("/execution/sentinel/enabled")->AsBool()) {
    const auto* ready_value = plan_view.Get("/execution/sentinel/ready_value");
    if (ready_value && ready_value->IsNumber() && ready_value->AsNumber() == 1.0) {
      emit(
          "CHK_SEM_STALENESS_SENTINEL_CONSTANT",
          "Sentinel readiness uses constant ready_value=1.0 and may not be monotonic.",
          "monotonic counter/timestamp-like readiness",
          "constant ready_value=1.0",
          "/execution/sentinel/ready_value");
    }
  }

  std::set<std::string> gate_ids;
  const auto* gates = plan_view.Get("/gates");
  if (gates && gates->IsObject()) {
    for (const auto& [k, _] : gates->AsObject().fields) {
      gate_ids.insert(k);
    }
  }

  // Same-bar leakage + gate correctness tripwires.
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
        const std::string step_base = "/chains/" + chain_id + "/steps/" + std::to_string(i);
        for (const auto& [field, value] : step.AsObject().fields) {
          if (!value.IsString()) {
            continue;
          }
          const std::string text = value.AsString();
          if (text.find("same_bar") != std::string::npos || text.find("@bar.current") != std::string::npos || text.find("[0]") != std::string::npos) {
            emit(
                "CHK_SEM_SAME_BAR_LEAKAGE",
                "Potential same-bar leakage marker in step string field.",
                "no same_bar/@bar.current/[0] leakage markers",
                text,
                step_base + "/" + field);
          }

          // Gate references in step fields must resolve.
          std::string token = "@gate.";
          size_t pos = text.find(token);
          while (pos != std::string::npos) {
            size_t start = pos + token.size();
            size_t end = start;
            while (end < text.size() && (std::isalnum(static_cast<unsigned char>(text[end])) || text[end] == '_')) {
              ++end;
            }
            const std::string gate_id = text.substr(start, end - start);
            if (!gate_id.empty() && !gate_ids.contains(gate_id)) {
              emit(
                  "CHK_SEM_GATE_EXISTS",
                  "Gate reference in step field does not resolve.",
                  "existing gate id",
                  gate_id,
                  "/gates/" + gate_id);
            }
            pos = text.find(token, end);
          }
        }
      }
    }
  }

  // Permutation drift / mutation tripwire: study_input permutations in bind mode under managed-only policy.
  const auto* perm_policy = plan_view.Get("/validation/sierra_study_input_permute_policy");
  const bool managed_only_policy =
      (!perm_policy) || (perm_policy->IsString() && perm_policy->AsString() == "managed_only");
  const auto* on_violation_ptr = plan_view.Get("/validation/sierra_study_input_permute_on_violation");
  const std::string on_violation =
      on_violation_ptr && on_violation_ptr->IsString() ? on_violation_ptr->AsString() : "error";
  const auto* permute = plan_view.Get("/parameters/permute");
  if (permute && permute->IsArray()) {
    for (size_t i = 0; i < permute->AsArray().items.size(); ++i) {
      const auto& p = permute->AsArray().items[i];
      if (!p.IsObject()) {
        continue;
      }
      const auto& pf = p.AsObject().fields;
      auto kind_it = pf.find("kind");
      if (kind_it == pf.end() || !kind_it->second.IsString()) {
        continue;
      }
      if (kind_it->second.AsString() != "study_input") {
        continue;
      }

      if (managed_only_policy) {
        std::string study_key;
        auto study_it = pf.find("study");
        if (study_it != pf.end() && study_it->second.IsString()) {
          study_key = study_it->second.AsString();
        }
        if (!study_key.empty()) {
          const auto* study_mode = plan_view.Get("/studies/" + study_key + "/mode");
          if (study_mode && study_mode->IsString() && study_mode->AsString() == "bind") {
            emit_policy(
                "CHK_SEM_PERMUTE_BIND_MODE_DRIFT",
                "study_input permutation targets bind-mode study under managed_only policy.",
                "managed study mode or allowlist_bind_mode policy",
                "bind",
                "/studies/" + study_key + "/mode",
                on_violation);
          }
        }
      }

      if (perm_policy && perm_policy->IsString() && perm_policy->AsString() == "disabled") {
        emit_policy(
            "CHK_SEM_PERMUTE_DISABLED_MUTATION",
            "study_input permutation declared while permutation policy is disabled.",
            "no study_input permutation items",
            "study_input permutation present",
            "/parameters/permute/" + std::to_string(i),
            on_violation);
      }
    }
  }

  // Dedupe boundary tripwire: if dedupe_key fields are declared, they should include session boundary.
  const auto* fields = plan_view.Get("/outputs/dataset/fields");
  if (fields && fields->IsArray()) {
    for (size_t i = 0; i < fields->AsArray().items.size(); ++i) {
      const auto& f = fields->AsArray().items[i];
      if (!f.IsObject()) {
        continue;
      }
      const auto& ff = f.AsObject().fields;
      auto id_it = ff.find("id");
      auto from_it = ff.find("from");
      if (id_it != ff.end() && id_it->second.IsString() && id_it->second.AsString().find("dedupe") != std::string::npos) {
        if (from_it == ff.end() || !from_it->second.IsString() || from_it->second.AsString().find("session") == std::string::npos) {
          emit(
              "CHK_SEM_DEDUPE_SESSION_BOUNDARY",
              "Dedupe key field missing session boundary component.",
              "dedupe key includes session boundary",
              from_it == ff.end() ? "missing" : from_it->second.AsString(),
              "/outputs/dataset/fields/" + std::to_string(i));
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
