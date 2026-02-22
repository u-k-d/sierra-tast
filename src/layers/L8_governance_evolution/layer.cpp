#include "generated/contracts/L8_governance_evolution_api.h"
#include "sre/runtime.hpp"
#include "src/layers/common/layer_common.hpp"

#include <set>

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
  auto emit_warn = [&](std::string check_id, std::string reason, std::string expected, std::string actual, std::string pointer) {
    Diagnostic d;
    d.code = "E_LAYER_GOVERNANCE_INVALID";
    d.layer_id = "governance_evolution";
    d.check_id = std::move(check_id);
    d.group_id = "CHKGRP_PLAN_AST_SHAPE";
    d.reason = std::move(reason);
    d.expected = std::move(expected);
    d.actual = std::move(actual);
    d.blame_pointers = {std::move(pointer)};
    d.remediation = "Adjust optional capabilities metadata or runtime supported capabilities list.";
    d.severity = "warn";
    d.source = "contracts/L8_governance_evolution.manifest.json";
    diag.Emit(d);
  };

  const std::set<std::string> supported_caps = {
      "cap:sre:core_identity_io:v1",
      "cap:sre:universe_data:v1",
      "cap:sre:sierra_runtime_topology:v1",
      "cap:sre:studies_features:v1",
      "cap:sre:rule_chain_dsl:v1",
      "cap:sre:experimentation_permute:v1",
      "cap:sre:semantics_integrity:v1",
      "cap:sre:outputs_repro:v1",
      "cap:sre:governance_evolution:v1",
  };

  const auto* dag = plan_view.Get("/dag");
  if (dag && !dag->IsNull() && !dag->IsObject()) {
    emit("CHK_DAG_TYPE", "dag must be object or null.", "object|null", dag->TypeName(), "/dag");
  } else if (dag && dag->IsObject()) {
    const auto& d = dag->AsObject().fields;
    auto nodes_it = d.find("nodes");
    if (nodes_it != d.end()) {
      if (!nodes_it->second.IsArray()) {
        emit("CHK_DAG_NODES_ARRAY", "dag.nodes must be array when provided.", "array", nodes_it->second.TypeName(), "/dag/nodes");
      } else {
        std::set<std::string> node_ids;
        for (size_t i = 0; i < nodes_it->second.AsArray().items.size(); ++i) {
          const auto& node = nodes_it->second.AsArray().items[i];
          const std::string base = "/dag/nodes/" + std::to_string(i);
          if (!node.IsObject()) {
            emit("CHK_DAG_NODE_OBJECT", "dag node must be object.", "object", node.TypeName(), base);
            continue;
          }
          const auto& nf = node.AsObject().fields;
          auto id_it = nf.find("id");
          if (id_it == nf.end() || !id_it->second.IsString() || id_it->second.AsString().empty()) {
            emit("CHK_DAG_NODE_ID", "dag node id is required.", "non-empty string", id_it == nf.end() ? "missing" : id_it->second.TypeName(), base + "/id");
            continue;
          }
          const std::string id = id_it->second.AsString();
          if (node_ids.contains(id)) {
            emit("CHK_DAG_NODE_ID_UNIQUE", "dag node id must be unique.", "unique id", id, base + "/id");
          }
          node_ids.insert(id);
        }
        for (size_t i = 0; i < nodes_it->second.AsArray().items.size(); ++i) {
          const auto& node = nodes_it->second.AsArray().items[i];
          if (!node.IsObject()) {
            continue;
          }
          const auto& nf = node.AsObject().fields;
          auto parents_it = nf.find("parents");
          if (parents_it != nf.end()) {
            if (!parents_it->second.IsArray()) {
              emit("CHK_DAG_NODE_PARENTS_ARRAY", "dag node parents must be array when provided.", "array", parents_it->second.TypeName(), "/dag/nodes/" + std::to_string(i) + "/parents");
            } else {
              for (size_t j = 0; j < parents_it->second.AsArray().items.size(); ++j) {
                const auto& p = parents_it->second.AsArray().items[j];
                if (!p.IsString() || p.AsString().empty()) {
                  emit("CHK_DAG_NODE_PARENT_VALUE", "dag parent id must be non-empty string.", "non-empty string", p.TypeName(), "/dag/nodes/" + std::to_string(i) + "/parents/" + std::to_string(j));
                  continue;
                }
                if (!node_ids.contains(p.AsString())) {
                  emit("CHK_DAG_NODE_PARENT_EXISTS", "dag parent id must refer to existing node.", "existing node id", p.AsString(), "/dag/nodes/" + std::to_string(i) + "/parents/" + std::to_string(j));
                }
              }
            }
          }
        }
      }
    }
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
        } else if (!supported_caps.contains(item.AsString())) {
          emit(
              "CHK_REQUIRED_CAP_SUPPORTED",
              "required capability is not supported by runtime.",
              "supported capability id",
              item.AsString(),
              "/meta/engine_capabilities_required/" + std::to_string(i));
        }
      }
    }
  }

  const auto* optional_caps = plan_view.Get("/meta/engine_capabilities_optional");
  if (optional_caps) {
    if (!optional_caps->IsArray()) {
      emit("CHK_OPTIONAL_CAPS_ARRAY", "engine_capabilities_optional must be array.", "array", optional_caps->TypeName(), "/meta/engine_capabilities_optional");
    } else {
      for (size_t i = 0; i < optional_caps->AsArray().items.size(); ++i) {
        const auto& item = optional_caps->AsArray().items[i];
        if (!item.IsString() || item.AsString().empty()) {
          emit(
              "CHK_OPTIONAL_CAP_ITEM",
              "optional capability ids must be non-empty strings.",
              "non-empty string",
              item.TypeName(),
              "/meta/engine_capabilities_optional/" + std::to_string(i));
        } else if (!supported_caps.contains(item.AsString())) {
          emit_warn(
              "CHK_OPTIONAL_CAP_UNSUPPORTED",
              "optional capability is not currently supported by runtime.",
              "supported capability id",
              item.AsString(),
              "/meta/engine_capabilities_optional/" + std::to_string(i));
        }
      }
    }
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
