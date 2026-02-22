#include "generated/contracts/L5_experimentation_permute_api.h"
#include "sre/runtime.hpp"
#include "src/layers/common/layer_common.hpp"

#include <map>
#include <set>

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

  auto emit_with_policy = [&](std::string check_id, std::string reason, std::string expected, std::string actual, std::string pointer, std::string on_violation) {
    if (on_violation == "error") {
      emit(std::move(check_id), std::move(reason), std::move(expected), std::move(actual), std::move(pointer));
      return;
    }
    Diagnostic d;
    d.code = "E_LAYER_PERMUTE_INVALID";
    d.layer_id = "experimentation_permute";
    d.check_id = std::move(check_id);
    d.group_id = "CHKGRP_PLAN_AST_SHAPE";
    d.reason = std::move(reason);
    d.expected = std::move(expected);
    d.actual = std::move(actual);
    d.blame_pointers = {std::move(pointer)};
    d.remediation = on_violation == "skip_permutation" ? "Skip this permutation and keep policy-compliant permutations only."
                                                        : "Adjust permutation policy or bind-mode allowlist.";
    d.severity = "warn";
    d.source = "contracts/L5_experimentation_permute.manifest.json";
    diag.Emit(d);
  };

  std::string permute_policy = "managed_only";
  std::string on_violation = "error";
  std::map<std::string, std::set<std::string>> bind_allow_inputs;

  const auto* validation = plan_view.Get("/validation");
  if (validation && validation->IsObject()) {
    const auto& v = validation->AsObject().fields;
    auto policy_it = v.find("sierra_study_input_permute_policy");
    if (policy_it != v.end()) {
      if (!policy_it->second.IsString()) {
        emit("CHK_PERMUTE_POLICY_TYPE", "sierra_study_input_permute_policy must be string.", "managed_only|allowlist_bind_mode|disabled", policy_it->second.TypeName(), "/validation/sierra_study_input_permute_policy");
      } else {
        permute_policy = policy_it->second.AsString();
      }
    }

    auto violation_it = v.find("sierra_study_input_permute_on_violation");
    if (violation_it != v.end()) {
      if (!violation_it->second.IsString()) {
        emit("CHK_PERMUTE_POLICY_ON_VIOLATION_TYPE", "sierra_study_input_permute_on_violation must be string.", "error|warn|skip_permutation", violation_it->second.TypeName(), "/validation/sierra_study_input_permute_on_violation");
      } else {
        on_violation = violation_it->second.AsString();
      }
    }

    auto allowlist_it = v.find("sierra_bind_mode_allowlist");
    if (allowlist_it != v.end()) {
      if (!allowlist_it->second.IsArray()) {
        emit("CHK_BIND_ALLOWLIST_ARRAY", "sierra_bind_mode_allowlist must be array.", "array", allowlist_it->second.TypeName(), "/validation/sierra_bind_mode_allowlist");
      } else {
        for (size_t i = 0; i < allowlist_it->second.AsArray().items.size(); ++i) {
          const auto& a = allowlist_it->second.AsArray().items[i];
          if (!a.IsObject()) {
            emit("CHK_BIND_ALLOWLIST_ITEM_OBJECT", "allowlist item must be object.", "object", a.TypeName(), "/validation/sierra_bind_mode_allowlist/" + std::to_string(i));
            continue;
          }
          const auto& af = a.AsObject().fields;
          auto study_key_it = af.find("study_key");
          if (study_key_it == af.end() || !study_key_it->second.IsString() || study_key_it->second.AsString().empty()) {
            emit("CHK_BIND_ALLOWLIST_STUDY_KEY", "allowlist.study_key is required.", "string", study_key_it == af.end() ? "missing" : study_key_it->second.TypeName(), "/validation/sierra_bind_mode_allowlist/" + std::to_string(i) + "/study_key");
            continue;
          }
          std::set<std::string> allowed_inputs;
          auto inputs_it = af.find("inputs_allow");
          if (inputs_it != af.end()) {
            if (!inputs_it->second.IsArray()) {
              emit("CHK_BIND_ALLOWLIST_INPUTS_ARRAY", "allowlist.inputs_allow must be array.", "array", inputs_it->second.TypeName(), "/validation/sierra_bind_mode_allowlist/" + std::to_string(i) + "/inputs_allow");
            } else {
              for (size_t j = 0; j < inputs_it->second.AsArray().items.size(); ++j) {
                const auto& in = inputs_it->second.AsArray().items[j];
                if (in.IsString() && !in.AsString().empty()) {
                  allowed_inputs.insert(in.AsString());
                }
              }
            }
          }
          bind_allow_inputs[study_key_it->second.AsString()] = std::move(allowed_inputs);
        }
      }
    }
  }

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

            if (study_it != it.end() && study_it->second.IsString() && input_it != it.end() && input_it->second.IsString()) {
              const std::string study_key = study_it->second.AsString();
              const std::string input_key = input_it->second.AsString();
              const auto* mode = plan_view.Get("/studies/" + study_key + "/mode");
              const std::string mode_actual = mode && mode->IsString() ? mode->AsString() : "missing";

              if (permute_policy == "disabled") {
                emit_with_policy(
                    "CHK_PERMUTE_POLICY_DISABLED",
                    "study_input permutation is disabled by policy.",
                    "no study_input permutation items",
                    "study_input item present",
                    base + "/kind",
                    on_violation);
              } else if (permute_policy == "managed_only") {
                if (mode_actual != "managed") {
                  emit_with_policy(
                      "CHK_PERMUTE_POLICY_MANAGED_ONLY",
                      "study_input permutation must target managed-mode study under managed_only policy.",
                      "managed",
                      mode_actual,
                      "/studies/" + study_key + "/mode",
                      on_violation);
                }
              } else if (permute_policy == "allowlist_bind_mode") {
                if (mode_actual == "bind") {
                  auto allow_it = bind_allow_inputs.find(study_key);
                  if (allow_it == bind_allow_inputs.end()) {
                    emit_with_policy(
                        "CHK_PERMUTE_BIND_ALLOWLIST_STUDY",
                        "bind-mode study is not allowlisted for study_input permutation.",
                        "study in sierra_bind_mode_allowlist",
                        study_key,
                        "/validation/sierra_bind_mode_allowlist",
                        on_violation);
                  } else if (!allow_it->second.contains(input_key)) {
                    emit_with_policy(
                        "CHK_PERMUTE_BIND_ALLOWLIST_INPUT",
                        "bind-mode input is not allowlisted for study_input permutation.",
                        "input in allowlist.inputs_allow",
                        input_key,
                        "/validation/sierra_bind_mode_allowlist",
                        on_violation);
                  }
                }
              }
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
