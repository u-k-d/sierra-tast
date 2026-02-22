#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sre {

class JsonValue {
public:
  struct Array;
  struct Object;

  enum class Kind {
    Null,
    Bool,
    Number,
    String,
    Array,
    Object,
  };

  JsonValue();
  JsonValue(std::nullptr_t);
  JsonValue(bool value);
  JsonValue(double value);
  JsonValue(const char* value);
  JsonValue(std::string value);
  JsonValue(Array value);
  JsonValue(Object value);

  Kind kind() const;
  bool IsNull() const;
  bool IsBool() const;
  bool IsNumber() const;
  bool IsString() const;
  bool IsArray() const;
  bool IsObject() const;

  bool AsBool() const;
  double AsNumber() const;
  const std::string& AsString() const;
  const Array& AsArray() const;
  const Object& AsObject() const;
  Array& AsArray();
  Object& AsObject();

  std::string TypeName() const;

private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};

struct JsonValue::Array {
  std::vector<JsonValue> items;
};

struct JsonValue::Object {
  std::map<std::string, JsonValue, std::less<>> fields;
};

JsonValue ParseJson(std::string_view input);
JsonValue ParseJsonFile(const std::filesystem::path& path);
std::string DumpCanonicalJson(const JsonValue& value);
const JsonValue* ResolvePointer(const JsonValue& value, std::string_view pointer);
bool PointerExists(const JsonValue& value, std::string_view pointer);

}  // namespace sre

namespace sre::layers {

struct Status {
  bool ok;
  static Status Success();
  static Status Failure();
};

struct Diagnostic {
  std::string code;
  std::string layer_id;
  std::string check_id;
  std::string group_id;
  std::string reason;
  std::string expected;
  std::string actual;
  std::vector<std::string> blame_pointers;
  std::string remediation;
  std::string severity;
  std::string source;
};

class DiagnosticSink {
public:
  void Emit(const Diagnostic& diag);
  bool HasErrors() const;
  const std::vector<Diagnostic>& Items() const;
  void WriteJsonl(const std::filesystem::path& path) const;
  void WriteHumanSummary(const std::filesystem::path& path) const;

private:
  std::vector<Diagnostic> items_;
};

class ScopedPlanView {
public:
  ScopedPlanView(const sre::JsonValue& plan, std::vector<std::string> owned_pointers);

  const sre::JsonValue& Plan() const;
  const sre::JsonValue* Get(std::string_view pointer) const;
  bool Exists(std::string_view pointer) const;
  bool PointerInScope(std::string_view pointer) const;

private:
  const sre::JsonValue* plan_;
  std::vector<std::string> owned_pointers_;
};

using LayerValidator = Status (*)(const ScopedPlanView&, DiagnosticSink&);

void EmitLayerError(
    DiagnosticSink& sink,
    std::string layer_id,
    std::string code,
    std::string check_id,
    std::string group_id,
    std::string reason,
    std::string expected,
    std::string actual,
    std::vector<std::string> blame_pointers,
    std::string remediation,
    std::string source = "");

bool EmitFixtureMarkerViolation(
    const ScopedPlanView& plan_view,
    DiagnosticSink& sink,
    std::string_view layer_id,
    std::string_view error_code,
    std::string_view default_pointer,
    std::string_view check_id);

// Layer functions (generated contracts define these signatures).
Status ValidateCoreIdentityIo(const ScopedPlanView& plan_view, DiagnosticSink& diag) noexcept;
Status ValidateUniverseData(const ScopedPlanView& plan_view, DiagnosticSink& diag) noexcept;
Status ValidateSierraRuntimeTopology(const ScopedPlanView& plan_view, DiagnosticSink& diag) noexcept;
Status ValidateStudiesFeatures(const ScopedPlanView& plan_view, DiagnosticSink& diag) noexcept;
Status ValidateRuleChainDsl(const ScopedPlanView& plan_view, DiagnosticSink& diag) noexcept;
Status ValidateExperimentationPermute(const ScopedPlanView& plan_view, DiagnosticSink& diag) noexcept;
Status ValidateSemanticsIntegrity(const ScopedPlanView& plan_view, DiagnosticSink& diag) noexcept;
Status ValidateOutputsRepro(const ScopedPlanView& plan_view, DiagnosticSink& diag) noexcept;
Status ValidateGovernanceEvolution(const ScopedPlanView& plan_view, DiagnosticSink& diag) noexcept;

void RegisterCoreIdentityIoApi() noexcept;
void RegisterUniverseDataApi() noexcept;
void RegisterSierraRuntimeTopologyApi() noexcept;
void RegisterStudiesFeaturesApi() noexcept;
void RegisterRuleChainDslApi() noexcept;
void RegisterExperimentationPermuteApi() noexcept;
void RegisterSemanticsIntegrityApi() noexcept;
void RegisterOutputsReproApi() noexcept;
void RegisterGovernanceEvolutionApi() noexcept;

}  // namespace sre::layers

namespace sre {

struct LayerManifest {
  std::string layer_id;
  std::vector<std::string> owned_pointers;
  std::string primary_error_code;
  std::filesystem::path path;
};

std::vector<LayerManifest> LoadLayerManifests(const std::filesystem::path& repo_root);
std::string HashCanonicalPlan(const JsonValue& value);

struct EngineResult {
  layers::Status status;
  JsonValue cnf_plan;
  std::vector<layers::Diagnostic> diagnostics;
};

class Engine {
public:
  explicit Engine(std::filesystem::path repo_root);
  EngineResult ValidatePlan(const JsonValue& raw_plan, const std::vector<std::string>& only_layers = {}) const;
  int ValidatePlanFile(
      const std::filesystem::path& plan_path,
      const std::filesystem::path& diagnostics_jsonl,
      const std::filesystem::path& diagnostics_summary,
      const std::vector<std::string>& only_layers = {}) const;

private:
  std::filesystem::path repo_root_;
  std::vector<LayerManifest> manifests_;
};

}  // namespace sre

