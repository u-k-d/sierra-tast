#include "sre/runtime.hpp"

#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {
thread_local std::string g_sre_last_error_message = "No error.";
}  // namespace

extern "C" __declspec(dllexport) const char* SREGetLastErrorMessage() {
  return g_sre_last_error_message.c_str();
}

extern "C" __declspec(dllexport) int SREPlanLintFileWithRepoRoot(
    const char* plan_path,
    const char* diagnostics_jsonl_path,
    const char* diagnostics_summary_path,
    const char* repo_root_path) {
  g_sre_last_error_message = "No error.";
  try {
    if (plan_path == nullptr || diagnostics_jsonl_path == nullptr || diagnostics_summary_path == nullptr) {
      g_sre_last_error_message =
          "Invalid arguments: plan_path/diagnostics_jsonl_path/diagnostics_summary_path cannot be null.";
      return 3;
    }
    if (plan_path[0] == '\0') {
      g_sre_last_error_message = "Plan path is empty. Set Study Input[0] to a valid JSON file path.";
      return 3;
    }

    std::filesystem::path repo_root = std::filesystem::current_path();
    if (repo_root_path != nullptr && repo_root_path[0] != '\0') {
      repo_root = std::filesystem::path(repo_root_path);
    }
    if (!std::filesystem::exists(repo_root / "contracts")) {
      std::ostringstream oss;
      oss << "Repo root is invalid or incomplete. Missing contracts directory: '" << (repo_root / "contracts").string()
          << "'. Set Study Input[3] (Repo Root) to your repo root path.";
      g_sre_last_error_message = oss.str();
      return 3;
    }

    std::filesystem::path resolved_plan = plan_path;
    if (resolved_plan.is_relative()) {
      resolved_plan = repo_root / resolved_plan;
    }
    std::filesystem::path resolved_jsonl = diagnostics_jsonl_path;
    if (resolved_jsonl.is_relative()) {
      resolved_jsonl = repo_root / resolved_jsonl;
    }
    std::filesystem::path resolved_summary = diagnostics_summary_path;
    if (resolved_summary.is_relative()) {
      resolved_summary = repo_root / resolved_summary;
    }

    sre::Engine engine(repo_root);
    const int rc = engine.ValidatePlanFile(resolved_plan, resolved_jsonl, resolved_summary, {});
    if (rc == 0) {
      g_sre_last_error_message = "Validation passed.";
    } else if (rc == 2) {
      std::ostringstream oss;
      oss << "Validation failed due to contract/semantic diagnostics. "
          << "RepoRoot='" << repo_root.string() << "'. "
          << "See diagnostics files: jsonl='" << resolved_jsonl.string() << "', summary='" << resolved_summary.string()
          << "'.";
      g_sre_last_error_message = oss.str();
    } else {
      std::ostringstream oss;
      oss << "Validation returned non-zero code " << rc << ". RepoRoot='" << repo_root.string() << "'.";
      g_sre_last_error_message = oss.str();
    }
    return rc;
  } catch (const std::exception& ex) {
    std::ostringstream oss;
    oss << "Unhandled exception in SREPlanLintFile: " << ex.what()
        << ". Likely causes: invalid plan path, missing contracts directory, or malformed JSON.";
    g_sre_last_error_message = oss.str();
    return 4;
  } catch (...) {
    g_sre_last_error_message = "Unhandled non-std exception in SREPlanLintFile.";
    return 4;
  }
}

extern "C" __declspec(dllexport) int SREPlanLintFile(
    const char* plan_path,
    const char* diagnostics_jsonl_path,
    const char* diagnostics_summary_path) {
  return SREPlanLintFileWithRepoRoot(plan_path, diagnostics_jsonl_path, diagnostics_summary_path, nullptr);
}

#if defined(SRE_ENABLE_ACSIL)
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include "sierrachart.h"
SCDLLName("SRE Runtime")

namespace acsil_runtime {

const sre::JsonValue* ObjectGet(const sre::JsonValue& object_value, std::string_view key) {
  if (!object_value.IsObject()) {
    return nullptr;
  }
  const auto& fields = object_value.AsObject().fields;
  auto it = fields.find(std::string(key));
  if (it == fields.end()) {
    return nullptr;
  }
  return &it->second;
}

std::string ToStdString(const SCString& value) {
  const char* chars = value.GetChars();
  return chars ? std::string(chars) : std::string();
}

bool JsonBoolAt(const sre::JsonValue& root, std::string_view pointer, bool default_value) {
  const auto* v = sre::ResolvePointer(root, pointer);
  if (!v || !v->IsBool()) {
    return default_value;
  }
  return v->AsBool();
}

std::string JsonStringAt(const sre::JsonValue& root, std::string_view pointer, std::string default_value) {
  const auto* v = sre::ResolvePointer(root, pointer);
  if (!v || !v->IsString() || v->AsString().empty()) {
    return default_value;
  }
  return v->AsString();
}

bool JsonIntegerAt(const sre::JsonValue& root, std::string_view pointer, int& out) {
  const auto* v = sre::ResolvePointer(root, pointer);
  if (!v || !v->IsNumber()) {
    return false;
  }
  const double n = v->AsNumber();
  if (std::fabs(n - std::round(n)) > 1e-9) {
    return false;
  }
  out = static_cast<int>(std::round(n));
  return true;
}

std::string ReplaceAll(std::string text, std::string_view needle, std::string_view replacement) {
  size_t pos = 0;
  while ((pos = text.find(needle, pos)) != std::string::npos) {
    text.replace(pos, needle.size(), replacement);
    pos += replacement.size();
  }
  return text;
}

std::filesystem::path ResolveOutputPath(
    const std::string& raw_path,
    const std::filesystem::path& repo_root,
    std::string_view symbol) {
  std::filesystem::path p = ReplaceAll(raw_path, "{symbol}", symbol);
  if (p.is_relative()) {
    p = repo_root / p;
  }
  return p;
}

std::string EscapeCsv(std::string_view cell) {
  bool needs_quotes = false;
  for (char c : cell) {
    if (c == ',' || c == '"' || c == '\n' || c == '\r') {
      needs_quotes = true;
      break;
    }
  }
  if (!needs_quotes) {
    return std::string(cell);
  }
  std::string out = "\"";
  for (char c : cell) {
    if (c == '"') {
      out += "\"\"";
    } else {
      out.push_back(c);
    }
  }
  out.push_back('"');
  return out;
}

std::string FormatNumber(double value) {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(6) << value;
  std::string out = oss.str();
  while (!out.empty() && out.back() == '0') {
    out.pop_back();
  }
  if (!out.empty() && out.back() == '.') {
    out.pop_back();
  }
  if (out == "-0") {
    return "0";
  }
  return out.empty() ? "0" : out;
}

bool ParseDouble(std::string_view text, double& out) {
  if (text.empty()) {
    return false;
  }
  char* end = nullptr;
  const std::string tmp(text);
  out = std::strtod(tmp.c_str(), &end);
  if (end == nullptr) {
    return false;
  }
  return static_cast<size_t>(end - tmp.c_str()) == tmp.size();
}

bool ParseInt(std::string_view text, int& out) {
  if (text.empty()) {
    return false;
  }
  char* end = nullptr;
  const std::string tmp(text);
  const long value = std::strtol(tmp.c_str(), &end, 10);
  if (end == nullptr || static_cast<size_t>(end - tmp.c_str()) != tmp.size()) {
    return false;
  }
  out = static_cast<int>(value);
  return true;
}

struct PlanDateRange {
  bool enabled = false;
  std::string start_date;
  std::string end_date;
};

PlanDateRange ParsePlanDateRange(const sre::JsonValue& plan) {
  PlanDateRange out;
  const std::string start = JsonStringAt(plan, "/universe/date_range/start", "");
  const std::string end = JsonStringAt(plan, "/universe/date_range/end", "");
  if (start.size() == 10 && end.size() == 10) {
    out.enabled = true;
    out.start_date = start;
    out.end_date = end;
  }
  return out;
}

std::string DatePartFromDateTimeText(std::string_view text) {
  if (text.size() >= 10) {
    return std::string(text.substr(0, 10));
  }
  return {};
}

bool DateInRange(std::string_view date_time_text, const PlanDateRange& range) {
  if (!range.enabled) {
    return true;
  }
  const std::string date = DatePartFromDateTimeText(date_time_text);
  if (date.size() != 10) {
    return false;
  }
  return date >= range.start_date && date <= range.end_date;
}

std::string DatasetSourceFromPlan(const sre::JsonValue& plan) {
  return JsonStringAt(plan, "/outputs/dataset/source", "bars");
}

void WriteJsonFile(const std::filesystem::path& path, const sre::JsonValue& value) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path);
  if (!out) {
    throw std::runtime_error("Failed to write JSON file: " + path.string());
  }
  out << sre::DumpCanonicalJson(value) << "\n";
}

void WriteCsvFile(
    const std::filesystem::path& path,
    const std::vector<std::string>& headers,
    const std::vector<std::vector<std::string>>& rows) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path);
  if (!out) {
    throw std::runtime_error("Failed to write CSV file: " + path.string());
  }
  for (size_t i = 0; i < headers.size(); ++i) {
    if (i > 0) {
      out << ",";
    }
    out << EscapeCsv(headers[i]);
  }
  out << "\n";
  for (const auto& row : rows) {
    for (size_t i = 0; i < row.size(); ++i) {
      if (i > 0) {
        out << ",";
      }
      out << EscapeCsv(row[i]);
    }
    out << "\n";
  }
}

void WriteJsonlFile(
    const std::filesystem::path& path,
    const std::vector<std::string>& headers,
    const std::vector<std::vector<std::string>>& rows) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path);
  if (!out) {
    throw std::runtime_error("Failed to write JSONL file: " + path.string());
  }
  for (const auto& row : rows) {
    sre::JsonValue::Object obj;
    for (size_t i = 0; i < headers.size(); ++i) {
      const std::string value = i < row.size() ? row[i] : "";
      obj.fields[headers[i]] = value;
    }
    out << sre::DumpCanonicalJson(sre::JsonValue(std::move(obj))) << "\n";
  }
}

std::vector<std::string> ParseCsvLine(std::string_view line) {
  std::vector<std::string> cells;
  std::string cell;
  bool in_quotes = false;
  for (size_t i = 0; i < line.size(); ++i) {
    const char c = line[i];
    if (in_quotes) {
      if (c == '"') {
        if (i + 1 < line.size() && line[i + 1] == '"') {
          cell.push_back('"');
          ++i;
        } else {
          in_quotes = false;
        }
      } else {
        cell.push_back(c);
      }
    } else {
      if (c == '"') {
        in_quotes = true;
      } else if (c == ',') {
        cells.push_back(cell);
        cell.clear();
      } else {
        cell.push_back(c);
      }
    }
  }
  cells.push_back(cell);
  return cells;
}

bool ReadCsvFile(
    const std::filesystem::path& path,
    std::vector<std::string>& headers,
    std::vector<std::vector<std::string>>& rows) {
  std::ifstream in(path);
  if (!in) {
    return false;
  }
  std::string line;
  bool first = true;
  while (std::getline(in, line)) {
    auto cells = ParseCsvLine(line);
    if (first) {
      headers = std::move(cells);
      first = false;
    } else if (!cells.empty()) {
      rows.push_back(std::move(cells));
    }
  }
  return !headers.empty();
}

void EmitRuntimeError(
    sre::layers::DiagnosticSink& sink,
    std::string code,
    std::string check_id,
    std::string reason,
    std::string expected,
    std::string actual,
    std::string pointer,
    std::string remediation = "Fix ACSIL runtime execution or plan inputs.") {
  sre::layers::EmitLayerError(
      sink,
      "outputs_repro",
      std::move(code),
      std::move(check_id),
      "CHKGRP_PLAN_AST_SHAPE",
      std::move(reason),
      std::move(expected),
      std::move(actual),
      {std::move(pointer)},
      std::move(remediation),
      "acsil_runtime");
}

void EmitRuntimeWarning(
    sre::layers::DiagnosticSink& sink,
    std::string code,
    std::string check_id,
    std::string reason,
    std::string expected,
    std::string actual,
    std::string pointer,
    std::string remediation = "Adjust plan or runtime inputs if this warning is unexpected.") {
  sre::layers::Diagnostic d;
  d.code = std::move(code);
  d.layer_id = "outputs_repro";
  d.check_id = std::move(check_id);
  d.group_id = "CHKGRP_PLAN_AST_SHAPE";
  d.reason = std::move(reason);
  d.expected = std::move(expected);
  d.actual = std::move(actual);
  d.blame_pointers = {std::move(pointer)};
  d.remediation = std::move(remediation);
  d.severity = "warning";
  d.source = "acsil_runtime";
  sink.Emit(d);
}

std::vector<std::string> PlanSymbols(const sre::JsonValue& plan, SCStudyInterfaceRef sc) {
  std::vector<std::string> symbols;
  const auto* value = sre::ResolvePointer(plan, "/universe/symbols");
  if (value && value->IsArray()) {
    for (const auto& s : value->AsArray().items) {
      if (s.IsString() && !s.AsString().empty()) {
        symbols.push_back(s.AsString());
      }
    }
  }
  if (symbols.empty()) {
    symbols.push_back(ToStdString(sc.Symbol));
  }
  return symbols;
}

int FindChartBySymbol(SCStudyInterfaceRef sc, const std::string& symbol) {
  if (ToStdString(sc.Symbol) == symbol) {
    return sc.ChartNumber;
  }
  const int highest = sc.GetHighestChartNumberUsedInChartBook();
  for (int c = 1; c <= highest; ++c) {
    if (ToStdString(sc.GetChartSymbol(c)) == symbol) {
      return c;
    }
  }
  return 0;
}

struct DatasetFieldSpec {
  std::string name;
  std::string source;
};

std::vector<DatasetFieldSpec> ParseDatasetFields(const sre::JsonValue& plan) {
  std::vector<DatasetFieldSpec> out;
  const auto* fields = sre::ResolvePointer(plan, "/outputs/dataset/fields");
  if (!fields || !fields->IsArray()) {
    return out;
  }
  for (size_t i = 0; i < fields->AsArray().items.size(); ++i) {
    const auto& f = fields->AsArray().items[i];
    if (!f.IsObject()) {
      continue;
    }
    const auto* name = ObjectGet(f, "name");
    const auto* id = ObjectGet(f, "id");
    const auto* ref = ObjectGet(f, "ref");
    const auto* from = ObjectGet(f, "from");
    DatasetFieldSpec spec;
    if (name && name->IsString() && !name->AsString().empty()) {
      spec.name = name->AsString();
    } else if (id && id->IsString() && !id->AsString().empty()) {
      spec.name = id->AsString();
    } else {
      spec.name = "field_" + std::to_string(i);
    }
    if (ref && ref->IsString()) {
      spec.source = ref->AsString();
    } else if (from && from->IsString()) {
      spec.source = from->AsString();
    }
    out.push_back(std::move(spec));
  }
  return out;
}

bool ValidateDatasetRefScopeAndEmitterCoverage(const sre::JsonValue& plan, sre::layers::DiagnosticSink& sink) {
  bool ok = true;
  const std::string dataset_source = DatasetSourceFromPlan(plan);
  if (dataset_source != "layer2_event_emitter") {
    return true;
  }

  std::set<std::string, std::less<>> emitted_columns = {"symbol", "bar_index"};
  const auto* emit_cols = sre::ResolvePointer(plan, "/execution/layer2/event_emitter/emit_columns");
  if (emit_cols && emit_cols->IsArray()) {
    for (const auto& c : emit_cols->AsArray().items) {
      if (!c.IsObject()) {
        continue;
      }
      const std::string name = JsonStringAt(c, "/name", "");
      if (!name.empty()) {
        emitted_columns.insert(name);
      }
    }
  }

  const auto* fields_v = sre::ResolvePointer(plan, "/outputs/dataset/fields");
  if (!fields_v || !fields_v->IsArray()) {
    return true;
  }

  std::set<std::string, std::less<>> missing_event_columns;
  for (size_t i = 0; i < fields_v->AsArray().items.size(); ++i) {
    const auto& f = fields_v->AsArray().items[i];
    if (!f.IsObject()) {
      continue;
    }
    const std::string field_name = JsonStringAt(f, "/name", "field_" + std::to_string(i));
    const auto check_ref = [&](const sre::JsonValue* ref_v, std::string_view key) {
      if (!ref_v || !ref_v->IsString()) {
        return;
      }
      const std::string ref = ref_v->AsString();
      if (ref.empty() || ref[0] != '@') {
        return;
      }
      if (ref.rfind("@event.", 0) != 0) {
        sre::layers::EmitLayerError(
            sink,
            "outputs_repro",
            "E_DATASET_REF_SCOPE_MISMATCH",
            "CHK_DATASET_REF_SCOPE_MISMATCH",
            "CHKGRP_PLAN_AST_SHAPE",
            "Dataset field ref namespace does not match outputs.dataset.source=layer2_event_emitter.",
            "ref namespace=@event.*",
            "dataset_source=" + dataset_source + ", field=" + field_name + ", actual_ref=" + ref,
            {"/outputs/dataset/source", "/outputs/dataset/fields/" + std::to_string(i) + "/" + std::string(key)},
            "Use @event.<column> and emit the column via execution.layer2.event_emitter.emit_columns",
            "acsil_runtime");
        ok = false;
        return;
      }
      const std::string col = ref.substr(7);
      if (col.empty() || !emitted_columns.contains(col)) {
        missing_event_columns.insert(col);
        sre::layers::EmitLayerError(
            sink,
            "outputs_repro",
            "E_EVENT_EMITTER_MISSING_COLUMN",
            "CHK_EVENT_EMITTER_COVERS_DATASET_FIELDS",
            "CHKGRP_PLAN_AST_SHAPE",
            "Dataset @event column is not produced by execution.layer2.event_emitter.emit_columns.",
            "dataset @event.<col> covered by emitted columns",
            "field=" + field_name + ", missing_event_col=" + col,
            {"/execution/layer2/event_emitter/emit_columns", "/outputs/dataset/fields/" + std::to_string(i)},
            "Add missing columns to execution.layer2.event_emitter.emit_columns or remove them from dataset.fields",
            "acsil_runtime");
        ok = false;
      }
    };
    check_ref(ObjectGet(f, "ref"), "ref");
    check_ref(ObjectGet(f, "from"), "from");
  }

  if (!missing_event_columns.empty()) {
    std::ostringstream emitted;
    bool first = true;
    for (const auto& c : emitted_columns) {
      if (!first) emitted << ",";
      first = false;
      emitted << c;
    }
    std::ostringstream missing;
    first = true;
    for (const auto& c : missing_event_columns) {
      if (!first) missing << ",";
      first = false;
      missing << c;
    }
    sre::layers::EmitLayerError(
        sink,
        "outputs_repro",
        "E_EVENT_EMITTER_MISSING_COLUMN",
        "CHK_EVENT_EMITTER_COVERS_DATASET_FIELDS",
        "CHKGRP_PLAN_AST_SHAPE",
        "One or more dataset @event columns are not emitted.",
        "all dataset @event columns emitted",
        "missing_event_columns=[" + missing.str() + "], emitted_columns=[" + emitted.str() + "]",
        {"/execution/layer2/event_emitter/emit_columns", "/outputs/dataset/fields"},
        "Add missing columns to execution.layer2.event_emitter.emit_columns or remove them from dataset.fields",
        "acsil_runtime");
    ok = false;
  }
  return ok;
}

bool ParseStudyRef(std::string_view source, std::string& study_key, std::string& output_key) {
  const std::string prefix = "@study.";
  if (source.rfind(prefix, 0) != 0) {
    return false;
  }
  std::string tail(source.substr(prefix.size()));
  const size_t dot = tail.find('.');
  if (dot == std::string::npos) {
    return false;
  }
  study_key = tail.substr(0, dot);
  output_key = tail.substr(dot + 1);
  return !study_key.empty() && !output_key.empty();
}

struct SymbolSnapshot {
  std::string symbol;
  int chart_number = 0;
  SCGraphData base_data;
  SCDateTimeArray date_times;
  int bar_index = -1;
};

bool LoadSymbolSnapshot(
    SCStudyInterfaceRef sc,
    const sre::JsonValue& plan,
    const std::string& symbol,
    SymbolSnapshot& snapshot,
    sre::layers::DiagnosticSink& sink) {
  snapshot.symbol = symbol;
  snapshot.chart_number = FindChartBySymbol(sc, symbol);
  if (snapshot.chart_number <= 0) {
    const bool strict = JsonBoolAt(
        plan, "/execution/backend/sierra_chart/layout_contract/readiness/symbol_resolution_strict", false);
    if (strict) {
      sre::layers::EmitLayerError(
          sink,
          "outputs_repro",
          "E_RUN_SYMBOL_CHART_NOT_FOUND_STRICT",
          "CHK_SYMBOL_CHART_RESOLUTION_STRICT",
          "CHKGRP_PLAN_AST_SHAPE",
          "Strict symbol resolution is enabled and symbol could not be resolved to a chart.",
          "resolved_chart in execution.worker_charts",
          "symbol=" + symbol + ", resolved_chart=null",
          {"/universe/symbols", "/execution/worker_charts"},
          "Load the symbol in the specified worker chart(s) or remove symbol from universe.symbols",
          "acsil_runtime");
    } else {
      sre::layers::Diagnostic d;
      d.code = "E_RUN_SYMBOL_CHART_NOT_FOUND";
      d.layer_id = "outputs_repro";
      d.check_id = "CHK_SYMBOL_CHART_RESOLUTION_STRICT";
      d.group_id = "CHKGRP_PLAN_AST_SHAPE";
      d.reason = "Symbol could not be resolved to a chart; continuing because symbol_resolution_strict=false.";
      d.expected = "resolved_chart in execution.worker_charts";
      d.actual = "symbol=" + symbol + ", resolved_chart=null";
      d.blame_pointers = {"/universe/symbols", "/execution/worker_charts"};
      d.remediation = "Load the symbol in the specified worker chart(s) or remove symbol from universe.symbols";
      d.severity = "warning";
      d.source = "acsil_runtime";
      sink.Emit(d);
    }
    return false;
  }

  sc.GetChartBaseData(snapshot.chart_number, snapshot.base_data);
  sc.GetChartDateTimeArray(snapshot.chart_number, snapshot.date_times);

  const int close_size = snapshot.base_data[SC_LAST].GetArraySize();
  const int dt_size = snapshot.date_times.GetArraySize();
  snapshot.bar_index = std::min(close_size, dt_size) - 1;
  if (snapshot.bar_index < 0) {
    EmitRuntimeWarning(
        sink,
        "E_RUN_DATA_UNAVAILABLE",
        "CHK_RUNTIME_BAR_DATA_UNAVAILABLE",
        "Chart does not have bar/time arrays available for dataset emission.",
        "at least one bar in chart arrays",
        symbol,
        "/outputs/dataset/fields",
        "Symbol will be skipped for this run. Load chart data before running again.");
    return false;
  }
  return true;
}

bool ResolveStudyBinding(
    const sre::JsonValue& plan,
    const std::string& study_key,
    const std::string& output_key,
    const SymbolSnapshot& snapshot,
    SCStudyInterfaceRef sc,
    int& out_chart_number,
    int& out_study_id,
    int& out_subgraph,
    sre::layers::DiagnosticSink& sink) {
  const auto* studies = sre::ResolvePointer(plan, "/studies");
  if (!studies || !studies->IsObject()) {
    EmitRuntimeError(
        sink,
        "E_NOT_IMPLEMENTED",
        "CHK_RUNTIME_STUDY_BINDING_NOT_IMPLEMENTED",
        "Study reference requested but studies block is not resolvable.",
        "/studies object",
        "missing /studies",
        "/studies");
    return false;
  }

  const auto& study_fields = studies->AsObject().fields;
  auto study_it = study_fields.find(study_key);
  if (study_it == study_fields.end() || !study_it->second.IsObject()) {
    EmitRuntimeError(
        sink,
        "E_RUN_DATA_UNAVAILABLE",
        "CHK_RUNTIME_STUDY_BINDING_MISSING",
        "Study reference cannot be resolved.",
        "study key exists in /studies",
        study_key,
        "/studies",
        "Declare the study in plan.studies and ensure it exists on chart.");
    return false;
  }
  const auto& study_spec = study_it->second;

  if (!JsonIntegerAt(study_spec, "/study_id", out_study_id) || out_study_id <= 0) {
    const auto* v = ObjectGet(study_spec, "study_id");
    EmitRuntimeError(
        sink,
        "E_NOT_IMPLEMENTED",
        "CHK_RUNTIME_STUDY_ID_NOT_IMPLEMENTED",
        "Study reference requires integer study_id.",
        "integer study_id",
        v && v->IsNumber() ? FormatNumber(v->AsNumber()) : "missing",
        "/studies/" + study_key + "/study_id");
    return false;
  }

  const auto* outputs = ObjectGet(study_spec, "outputs");
  if (!outputs || !outputs->IsObject()) {
    EmitRuntimeError(
        sink,
        "E_NOT_IMPLEMENTED",
        "CHK_RUNTIME_STUDY_OUTPUT_BINDING_NOT_IMPLEMENTED",
        "Study reference requires outputs map in studies block.",
        "outputs object with named output",
        "missing outputs",
        "/studies/" + study_key + "/outputs");
    return false;
  }
  const auto& output_fields = outputs->AsObject().fields;
  auto output_it = output_fields.find(output_key);
  if (output_it == output_fields.end() || !output_it->second.IsObject()) {
    EmitRuntimeError(
        sink,
        "E_RUN_DATA_UNAVAILABLE",
        "CHK_RUNTIME_STUDY_OUTPUT_BINDING_MISSING",
        "Requested study output key is not declared.",
        "output key exists under studies.<study>.outputs",
        output_key,
        "/studies/" + study_key + "/outputs",
        "Declare output mapping for the referenced output key.");
    return false;
  }
  const auto& output_spec = output_it->second;

  if (!JsonIntegerAt(output_spec, "/subgraph", out_subgraph) || out_subgraph < 0) {
    const auto* sg = ObjectGet(output_spec, "subgraph");
    EmitRuntimeError(
        sink,
        "E_NOT_IMPLEMENTED",
        "CHK_RUNTIME_STUDY_SUBGRAPH_NOT_IMPLEMENTED",
        "Study output mapping requires integer subgraph index.",
        "integer subgraph >= 0",
        sg && sg->IsNumber() ? FormatNumber(sg->AsNumber()) : "missing",
        "/studies/" + study_key + "/outputs/" + output_key + "/subgraph");
    return false;
  }

  std::string chart_ref = "worker";
  const auto* chart_ref_v = ObjectGet(study_spec, "chart_ref");
  if (chart_ref_v && chart_ref_v->IsString() && !chart_ref_v->AsString().empty()) {
    chart_ref = chart_ref_v->AsString();
  }

  if (chart_ref == "worker") {
    out_chart_number = snapshot.chart_number;
    return true;
  }
  if (chart_ref == "controller") {
    int controller = sc.ChartNumber;
    if (JsonIntegerAt(plan, "/execution/controller_chart", controller) && controller > 0) {
      out_chart_number = controller;
    } else {
      out_chart_number = sc.ChartNumber;
    }
    return true;
  }
  if (chart_ref == "current") {
    out_chart_number = sc.ChartNumber;
    return true;
  }

  EmitRuntimeError(
      sink,
      "E_NOT_IMPLEMENTED",
      "CHK_RUNTIME_STUDY_CHART_REF_NOT_IMPLEMENTED",
      "Unsupported studies.<study>.chart_ref for ACSIL runtime binding.",
      "worker|controller|current",
      chart_ref,
      "/studies/" + study_key + "/chart_ref");
  return false;
}

bool ResolveDatasetFieldValue(
    SCStudyInterfaceRef sc,
    const sre::JsonValue& plan,
    const DatasetFieldSpec& field,
    const SymbolSnapshot& snapshot,
    int bar_index,
    std::string& out_value,
    sre::layers::DiagnosticSink& sink) {
  const std::string& source = field.source;
  if (source.empty()) {
    out_value.clear();
    return true;
  }
  if (source[0] != '@') {
    out_value = source;
    return true;
  }

  if (source == "@symbol") {
    out_value = snapshot.symbol;
    return true;
  }

  if (source == "@bar.datetime") {
    if (bar_index < 0 || bar_index >= snapshot.date_times.GetArraySize()) {
      out_value.clear();
      return false;
    }
    out_value = ToStdString(sc.DateTimeToString(snapshot.date_times[bar_index], FLAG_DT_COMPLETE_DATETIME));
    return true;
  }

  auto bar_value = [&](int field_id) -> std::string {
    const auto& arr = snapshot.base_data[field_id];
    if (bar_index >= 0 && bar_index < arr.GetArraySize()) {
      return FormatNumber(arr[bar_index]);
    }
    return std::string();
  };

  if (source == "@bar.open") {
    out_value = bar_value(SC_OPEN);
    if (!out_value.empty()) {
      return true;
    }
  } else if (source == "@bar.high") {
    out_value = bar_value(SC_HIGH);
    if (!out_value.empty()) {
      return true;
    }
  } else if (source == "@bar.low") {
    out_value = bar_value(SC_LOW);
    if (!out_value.empty()) {
      return true;
    }
  } else if (source == "@bar.close") {
    out_value = bar_value(SC_LAST);
    if (!out_value.empty()) {
      return true;
    }
  } else if (source == "@bar.volume") {
    out_value = bar_value(SC_VOLUME);
    if (!out_value.empty()) {
      return true;
    }
  }

  std::string study_key;
  std::string output_key;
  if (ParseStudyRef(source, study_key, output_key)) {
    int chart_number = 0;
    int study_id = 0;
    int subgraph = 0;
    if (!ResolveStudyBinding(
            plan,
            study_key,
            output_key,
            snapshot,
            sc,
            chart_number,
            study_id,
            subgraph,
            sink)) {
      return false;
    }

    SCFloatArray study_array;
    sc.GetStudyArrayFromChartUsingID(chart_number, study_id, subgraph, study_array);
    if (study_array.GetArraySize() <= 0) {
      EmitRuntimeError(
          sink,
          "E_RUN_DATA_UNAVAILABLE",
          "CHK_RUNTIME_STUDY_ARRAY_UNAVAILABLE",
          "Study subgraph array was not retrievable from chart/study mapping.",
          "non-empty study array",
          source,
          "/studies/" + study_key,
          "Ensure study_id/subgraph exist on the target chart and are loaded.");
      return false;
    }

    int idx = bar_index;
    if (chart_number != snapshot.chart_number) {
      if (bar_index < 0 || bar_index >= snapshot.date_times.GetArraySize()) {
        idx = -1;
      } else {
        idx = sc.GetNearestMatchForSCDateTime(chart_number, snapshot.date_times[bar_index]);
      }
    }
    if (idx < 0 || idx >= study_array.GetArraySize()) {
      idx = study_array.GetArraySize() - 1;
    }
    out_value = FormatNumber(study_array[idx]);
    return true;
  }

  EmitRuntimeError(
      sink,
      "E_NOT_IMPLEMENTED",
      "CHK_RUNTIME_DATASET_REF_NOT_IMPLEMENTED",
      "Dataset field reference is not implemented by ACSIL runtime.",
      "@symbol|@bar.*|@study.<study>.<output>",
      source,
      "/outputs/dataset/fields",
      "Use supported refs or extend ACSIL runtime resolver.");
  return false;
}

enum class L2ValueKind {
  Number,
  Bool,
  String,
};

struct L2Value {
  L2ValueKind kind = L2ValueKind::Number;
  double number = 0.0;
  bool boolean = false;
  std::string text;
};

L2Value MakeNumber(double value) {
  L2Value out;
  out.kind = L2ValueKind::Number;
  out.number = value;
  out.boolean = (std::fabs(value) > 1e-12);
  return out;
}

L2Value MakeBool(bool value) {
  L2Value out;
  out.kind = L2ValueKind::Bool;
  out.boolean = value;
  out.number = value ? 1.0 : 0.0;
  return out;
}

L2Value MakeString(std::string value) {
  L2Value out;
  out.kind = L2ValueKind::String;
  out.text = std::move(value);
  return out;
}

double L2ToNumber(const L2Value& value) {
  if (value.kind == L2ValueKind::Number) {
    return value.number;
  }
  if (value.kind == L2ValueKind::Bool) {
    return value.boolean ? 1.0 : 0.0;
  }
  double parsed = 0.0;
  if (ParseDouble(value.text, parsed)) {
    return parsed;
  }
  return 0.0;
}

bool L2ToBool(const L2Value& value) {
  if (value.kind == L2ValueKind::Bool) {
    return value.boolean;
  }
  if (value.kind == L2ValueKind::Number) {
    return std::fabs(value.number) > 1e-12;
  }
  return !value.text.empty() && value.text != "0" && value.text != "false" && value.text != "False";
}

std::string L2ToString(const L2Value& value) {
  if (value.kind == L2ValueKind::String) {
    return value.text;
  }
  if (value.kind == L2ValueKind::Bool) {
    return value.boolean ? "true" : "false";
  }
  return FormatNumber(value.number);
}

bool IsKnownLayer2Op(std::string_view kind) {
  static const std::set<std::string, std::less<>> kOps = {
      "sma", "ema", "atr", "roc", "diff", "add", "sub", "mul", "div", "gt", "gte", "lt", "lte", "eq", "neq", "and",
      "or", "not", "abs", "clip"};
  return kOps.find(std::string(kind)) != kOps.end();
}

bool IsKnownLayer2NodeKind(std::string_view kind) {
  return kind == "param" || kind == "bar_field" || kind == "study_ref" || IsKnownLayer2Op(kind);
}

bool BuildLayer2TopoOrder(
    const sre::JsonValue::Object& nodes,
    std::vector<std::string>& topo,
    sre::layers::DiagnosticSink& sink) {
  std::unordered_map<std::string, int> indegree;
  std::unordered_map<std::string, std::vector<std::string>> next;
  bool ok = true;

  for (const auto& [id, _] : nodes.fields) {
    indegree[id] = 0;
  }

  for (const auto& [id, node] : nodes.fields) {
    if (!node.IsObject()) {
      EmitRuntimeError(
          sink,
          "E_L2_UNKNOWN_NODE_KIND",
          "CHK_L2_NODE_KIND",
          "Layer2 node must be an object.",
          "object",
          node.TypeName(),
          "/execution/layer2/indicator_dag/nodes/" + id,
          "Define each indicator_dag node as an object.");
      ok = false;
      continue;
    }
    const auto* kind_v = ObjectGet(node, "kind");
    const std::string kind = (kind_v && kind_v->IsString()) ? kind_v->AsString() : "";
    if (!IsKnownLayer2NodeKind(kind)) {
      EmitRuntimeError(
          sink,
          "E_L2_UNKNOWN_NODE_KIND",
          "CHK_L2_NODE_KIND",
          "Layer2 node kind is unknown.",
          "param|bar_field|study_ref|supported op kind",
          kind.empty() ? (kind_v ? kind_v->TypeName() : "missing") : kind,
          "/execution/layer2/indicator_dag/nodes/" + id + "/kind",
          "Use a supported Layer2 node kind.");
      ok = false;
      continue;
    }
    if (!IsKnownLayer2Op(kind)) {
      continue;
    }

    const auto* inputs = ObjectGet(node, "inputs");
    if (!inputs || !inputs->IsArray() || inputs->AsArray().items.empty()) {
      EmitRuntimeError(
          sink,
          "E_L2_MISSING_NODE_DEP",
          "CHK_L2_NODE_DEP",
          "Layer2 op node requires one or more dependency inputs.",
          "inputs array[minItems=1]",
          inputs ? inputs->TypeName() : "missing",
          "/execution/layer2/indicator_dag/nodes/" + id + "/inputs",
          "Declare input node ids for this operation.");
      ok = false;
      continue;
    }
    for (size_t i = 0; i < inputs->AsArray().items.size(); ++i) {
      const auto& dep = inputs->AsArray().items[i];
      if (!dep.IsString() || dep.AsString().empty()) {
        EmitRuntimeError(
            sink,
            "E_L2_MISSING_NODE_DEP",
            "CHK_L2_NODE_DEP",
            "Layer2 op dependency entry must be a node id string.",
            "non-empty node id string",
            dep.TypeName(),
            "/execution/layer2/indicator_dag/nodes/" + id + "/inputs/" + std::to_string(i),
            "Replace with a declared node id.");
        ok = false;
        continue;
      }
      if (!indegree.contains(dep.AsString())) {
        EmitRuntimeError(
            sink,
            "E_L2_MISSING_NODE_DEP",
            "CHK_L2_NODE_DEP",
            "Layer2 op dependency is missing from indicator_dag.nodes.",
            "declared dependency node",
            dep.AsString(),
            "/execution/layer2/indicator_dag/nodes/" + id + "/inputs/" + std::to_string(i),
            "Declare the dependency node in indicator_dag.nodes.");
        ok = false;
        continue;
      }
      indegree[id] += 1;
      next[dep.AsString()].push_back(id);
    }
  }

  if (!ok) {
    return false;
  }

  std::set<std::string, std::less<>> ready;
  for (const auto& [id, deg] : indegree) {
    if (deg == 0) {
      ready.insert(id);
    }
  }

  while (!ready.empty()) {
    const std::string id = *ready.begin();
    ready.erase(ready.begin());
    topo.push_back(id);
    auto it = next.find(id);
    if (it == next.end()) {
      continue;
    }
    std::sort(it->second.begin(), it->second.end());
    for (const auto& child : it->second) {
      auto deg_it = indegree.find(child);
      if (deg_it == indegree.end()) {
        continue;
      }
      deg_it->second -= 1;
      if (deg_it->second == 0) {
        ready.insert(child);
      }
    }
  }

  if (topo.size() != nodes.fields.size()) {
    EmitRuntimeError(
        sink,
        "E_L2_DAG_CYCLE",
        "CHK_L2_DAG_CYCLE",
        "Layer2 indicator_dag contains a cycle.",
        "acyclic DAG",
        "cycle detected",
        "/execution/layer2/indicator_dag/nodes",
        "Remove cyclic dependencies between Layer2 nodes.");
    return false;
  }

  return true;
}

double SnapshotBarFieldAsNumber(const SymbolSnapshot& snapshot, int bar_index, std::string_view field) {
  auto field_at = [&](int field_id) -> double {
    const auto& arr = snapshot.base_data[field_id];
    if (bar_index < 0 || bar_index >= arr.GetArraySize()) {
      return 0.0;
    }
    return static_cast<double>(arr[bar_index]);
  };

  if (field == "open") return field_at(SC_OPEN);
  if (field == "high") return field_at(SC_HIGH);
  if (field == "low") return field_at(SC_LOW);
  if (field == "close") return field_at(SC_LAST);
  if (field == "volume") return field_at(SC_VOLUME);
  if (field == "vwap") {
    const double h = field_at(SC_HIGH);
    const double l = field_at(SC_LOW);
    const double c = field_at(SC_LAST);
    return (h + l + c) / 3.0;
  }
  return 0.0;
}

std::vector<L2Value> BuildLayer2StudyRefSeries(
    SCStudyInterfaceRef sc,
    const sre::JsonValue& plan,
    const SymbolSnapshot& snapshot,
    std::string_view ref,
    sre::layers::DiagnosticSink& sink,
    const std::string& pointer) {
  std::string study_key;
  std::string output_key;
  if (!ParseStudyRef(ref, study_key, output_key)) {
    EmitRuntimeError(
        sink,
        "E_L2_MISSING_NODE_DEP",
        "CHK_L2_STUDY_REF_RESOLVE",
        "Layer2 study_ref must use @study.<key>.<output> format.",
        "@study.<key>.<output>",
        std::string(ref),
        pointer,
        "Fix the study_ref format and ensure the study output exists.");
    return {};
  }

  int chart_number = 0;
  int study_id = 0;
  int subgraph = 0;
  if (!ResolveStudyBinding(plan, study_key, output_key, snapshot, sc, chart_number, study_id, subgraph, sink)) {
    return {};
  }

  SCFloatArray study_array;
  sc.GetStudyArrayFromChartUsingID(chart_number, study_id, subgraph, study_array);
  if (study_array.GetArraySize() <= 0) {
    EmitRuntimeError(
        sink,
        "E_L2_MISSING_NODE_DEP",
        "CHK_L2_STUDY_REF_RESOLVE",
        "Layer2 study_ref could not load study output array.",
        "non-empty study array",
        std::string(ref),
        pointer,
        "Ensure the bound study output is available on chart.");
    return {};
  }

  const int row_count = std::min(snapshot.base_data[SC_LAST].GetArraySize(), snapshot.date_times.GetArraySize());
  std::vector<L2Value> series;
  series.reserve(std::max(0, row_count));
  for (int i = 0; i < row_count; ++i) {
    int idx = i;
    if (chart_number != snapshot.chart_number) {
      idx = sc.GetNearestMatchForSCDateTime(chart_number, snapshot.date_times[i]);
    }
    if (idx < 0 || idx >= study_array.GetArraySize()) {
      idx = std::min(std::max(0, i), study_array.GetArraySize() - 1);
    }
    series.push_back(MakeNumber(study_array[idx]));
  }
  return series;
}

L2Value EvaluateLayer2OpAt(
    std::string_view op,
    const std::vector<std::vector<L2Value>>& inputs,
    int bar_index,
    int window_bars) {
  auto in = [&](size_t input_id, int idx) -> const L2Value& {
    static const L2Value kDefault = MakeNumber(0.0);
    if (input_id >= inputs.size()) {
      return kDefault;
    }
    if (idx < 0 || idx >= static_cast<int>(inputs[input_id].size())) {
      return kDefault;
    }
    return inputs[input_id][idx];
  };

  if (op == "sma") {
    const int window = std::max(1, window_bars);
    const int start = std::max(0, bar_index - window + 1);
    double sum = 0.0;
    int count = 0;
    for (int i = start; i <= bar_index; ++i) {
      sum += L2ToNumber(in(0, i));
      ++count;
    }
    return MakeNumber(count > 0 ? sum / static_cast<double>(count) : 0.0);
  }
  if (op == "roc") {
    const int lag = std::max(1, window_bars);
    const double prev = L2ToNumber(in(0, bar_index - lag));
    if (std::fabs(prev) < 1e-12) {
      return MakeNumber(0.0);
    }
    return MakeNumber((L2ToNumber(in(0, bar_index)) - prev) / prev);
  }
  if (op == "gt") return MakeBool(L2ToNumber(in(0, bar_index)) > L2ToNumber(in(1, bar_index)));
  if (op == "gte") return MakeBool(L2ToNumber(in(0, bar_index)) >= L2ToNumber(in(1, bar_index)));
  if (op == "lt") return MakeBool(L2ToNumber(in(0, bar_index)) < L2ToNumber(in(1, bar_index)));
  if (op == "lte") return MakeBool(L2ToNumber(in(0, bar_index)) <= L2ToNumber(in(1, bar_index)));
  if (op == "eq") return MakeBool(L2ToString(in(0, bar_index)) == L2ToString(in(1, bar_index)));
  if (op == "neq") return MakeBool(L2ToString(in(0, bar_index)) != L2ToString(in(1, bar_index)));
  if (op == "and") {
    bool value = true;
    for (size_t i = 0; i < inputs.size(); ++i) value = value && L2ToBool(in(i, bar_index));
    return MakeBool(value);
  }
  if (op == "or") {
    bool value = false;
    for (size_t i = 0; i < inputs.size(); ++i) value = value || L2ToBool(in(i, bar_index));
    return MakeBool(value);
  }
  if (op == "not") return MakeBool(!L2ToBool(in(0, bar_index)));
  if (op == "add") return MakeNumber(L2ToNumber(in(0, bar_index)) + L2ToNumber(in(1, bar_index)));
  if (op == "sub" || op == "diff") return MakeNumber(L2ToNumber(in(0, bar_index)) - L2ToNumber(in(1, bar_index)));
  if (op == "mul") return MakeNumber(L2ToNumber(in(0, bar_index)) * L2ToNumber(in(1, bar_index)));
  if (op == "div") {
    const double denom = L2ToNumber(in(1, bar_index));
    return MakeNumber(std::fabs(denom) < 1e-12 ? 0.0 : (L2ToNumber(in(0, bar_index)) / denom));
  }
  if (op == "abs") return MakeNumber(std::fabs(L2ToNumber(in(0, bar_index))));
  if (op == "clip") {
    const double x = L2ToNumber(in(0, bar_index));
    const double lo = L2ToNumber(in(1, bar_index));
    const double hi = L2ToNumber(in(2, bar_index));
    return MakeNumber(std::min(hi, std::max(lo, x)));
  }
  return MakeNumber(0.0);
}

std::string CoerceEmitType(std::string_view value, std::string_view type_name) {
  if (type_name == "string" || type_name == "datetime") {
    return std::string(value);
  }
  if (type_name == "bool") {
    if (value == "1" || value == "true" || value == "True") {
      return "true";
    }
    return "false";
  }
  if (type_name == "int") {
    int parsed = 0;
    if (ParseInt(value, parsed)) {
      return std::to_string(parsed);
    }
    double as_double = 0.0;
    if (ParseDouble(value, as_double)) {
      return std::to_string(static_cast<int>(std::llround(as_double)));
    }
    return "0";
  }
  double parsed = 0.0;
  if (ParseDouble(value, parsed)) {
    return FormatNumber(parsed);
  }
  return "0";
}

struct Layer2EventRow {
  std::string symbol;
  int bar_index = 0;
  std::unordered_map<std::string, std::string> columns;
};

struct Layer2EvaluationResult {
  bool ok = true;
  std::unordered_map<std::string, std::vector<Layer2EventRow>> events_by_symbol;
  std::unordered_map<std::string, SymbolSnapshot> snapshots_by_symbol;
  std::vector<std::string> accessible_symbols;
};

bool Layer2Enabled(const sre::JsonValue& plan) {
  const auto* layer2 = sre::ResolvePointer(plan, "/execution/layer2");
  if (!layer2 || !layer2->IsObject()) {
    return false;
  }
  return JsonBoolAt(plan, "/execution/layer2/enabled", true);
}

std::string Layer2SymbolFailurePolicy(const sre::JsonValue& plan) {
  std::string policy = JsonStringAt(plan, "/execution/layer2/symbol_failure_policy", "fail_fast");
  if (policy != "fail_fast" && policy != "skip_symbol") {
    policy = "fail_fast";
  }
  return policy;
}

void RelayDiagnostics(
    const sre::layers::DiagnosticSink& from,
    sre::layers::DiagnosticSink& into,
    bool downgrade_errors,
    std::string_view symbol) {
  for (const auto& item : from.Items()) {
    auto d = item;
    if (!symbol.empty()) {
      if (d.actual.empty()) {
        d.actual = "symbol=" + std::string(symbol);
      } else if (d.actual.find("symbol=") == std::string::npos) {
        d.actual += ", symbol=" + std::string(symbol);
      }
    }
    if (downgrade_errors && d.severity == "error") {
      d.severity = "warning";
    }
    into.Emit(d);
  }
}

std::string FirstErrorReason(const sre::layers::DiagnosticSink& sink) {
  for (const auto& d : sink.Items()) {
    if (d.severity == "error") {
      return d.reason;
    }
  }
  return std::string();
}

Layer2EvaluationResult EvaluateLayer2(
    SCStudyInterfaceRef sc,
    const sre::JsonValue& plan,
    const std::vector<std::string>& symbols,
    sre::layers::DiagnosticSink& sink) {
  Layer2EvaluationResult result;
  if (!Layer2Enabled(plan)) {
    return result;
  }

  const auto* nodes_value = sre::ResolvePointer(plan, "/execution/layer2/indicator_dag/nodes");
  if (!nodes_value || !nodes_value->IsObject() || nodes_value->AsObject().fields.empty()) {
    EmitRuntimeError(
        sink,
        "E_L2_MISSING_NODE_DEP",
        "CHK_L2_NODES_PRESENT",
        "Layer2 requires indicator_dag.nodes to be a non-empty object.",
        "non-empty object",
        nodes_value ? nodes_value->TypeName() : "missing",
        "/execution/layer2/indicator_dag/nodes",
        "Declare Layer2 indicator DAG nodes.");
    result.ok = false;
    return result;
  }

  std::vector<std::string> topo;
  if (!BuildLayer2TopoOrder(nodes_value->AsObject(), topo, sink)) {
    result.ok = false;
    return result;
  }

  const std::string trigger_node = JsonStringAt(plan, "/execution/layer2/event_emitter/trigger_node", "");
  if (trigger_node.empty() || !nodes_value->AsObject().fields.contains(trigger_node)) {
    EmitRuntimeError(
        sink,
        "E_L2_MISSING_NODE_DEP",
        "CHK_L2_TRIGGER_NODE",
        "Layer2 event_emitter.trigger_node must reference an existing node.",
        "existing node id",
        trigger_node.empty() ? "missing" : trigger_node,
        "/execution/layer2/event_emitter/trigger_node",
        "Set trigger_node to a declared DAG node.");
    result.ok = false;
    return result;
  }

  const auto* emit_columns = sre::ResolvePointer(plan, "/execution/layer2/event_emitter/emit_columns");
  if (!emit_columns || !emit_columns->IsArray() || emit_columns->AsArray().items.empty()) {
    EmitRuntimeError(
        sink,
        "E_L2_MISSING_NODE_DEP",
        "CHK_L2_EMIT_COLUMNS",
        "Layer2 event_emitter.emit_columns is required.",
        "emit_columns array[minItems=1]",
        emit_columns ? emit_columns->TypeName() : "missing",
        "/execution/layer2/event_emitter/emit_columns",
        "Declare one or more event output columns.");
    result.ok = false;
    return result;
  }

  const std::string emit_mode = JsonStringAt(plan, "/execution/layer2/event_emitter/emit_mode", "on_true_edge");
  const int cooldown = std::max(0, static_cast<int>(std::llround(
                                       sre::ResolvePointer(plan, "/execution/layer2/event_emitter/cooldown_bars")
                                           && sre::ResolvePointer(plan, "/execution/layer2/event_emitter/cooldown_bars")->IsNumber()
                                           ? sre::ResolvePointer(plan, "/execution/layer2/event_emitter/cooldown_bars")->AsNumber()
                                           : 0.0)));
  const bool require_final_bar = JsonBoolAt(plan, "/execution/layer2/require_final_bar", true);
  const PlanDateRange date_range = ParsePlanDateRange(plan);
  const bool skip_symbol_failures = Layer2SymbolFailurePolicy(plan) == "skip_symbol";
  const bool strict_symbol_resolution =
      JsonBoolAt(plan, "/execution/backend/sierra_chart/layout_contract/readiness/symbol_resolution_strict", false);

  for (const auto& symbol : symbols) {
    sre::layers::DiagnosticSink symbol_sink;
    SymbolSnapshot snapshot;
    if (!LoadSymbolSnapshot(sc, plan, symbol, snapshot, symbol_sink)) {
      const bool downgrade = skip_symbol_failures && !strict_symbol_resolution;
      RelayDiagnostics(symbol_sink, sink, downgrade, symbol);
      if (symbol_sink.HasErrors() && !downgrade) {
        result.ok = false;
        break;
      }
      if (symbol_sink.HasErrors() && downgrade) {
        EmitRuntimeWarning(
            sink,
            "E_RUN_SYMBOL_SKIPPED",
            "CHK_RUNTIME_SYMBOL_SKIPPED",
            "Symbol failed Layer2 snapshot resolution and was skipped due symbol_failure_policy=skip_symbol.",
            "symbol available for Layer2 evaluation",
            symbol,
            "/execution/layer2/symbol_failure_policy",
            "Set symbol_failure_policy=fail_fast to stop on first symbol failure.");
      }
      continue;
    }
    const int row_count = std::min(snapshot.base_data[SC_LAST].GetArraySize(), snapshot.date_times.GetArraySize());
    if (row_count <= 0) {
      continue;
    }

    std::unordered_map<std::string, std::vector<L2Value>> node_values;
    bool symbol_ok = true;
    for (const auto& node_id : topo) {
      const auto& node = nodes_value->AsObject().fields.at(node_id);
      const auto* kind_v = ObjectGet(node, "kind");
      const std::string kind = kind_v && kind_v->IsString() ? kind_v->AsString() : "";
      std::vector<L2Value> series;
      series.reserve(row_count);

      if (kind == "param") {
        const std::string type = JsonStringAt(node, "/type", "float");
        const auto* value_v = ObjectGet(node, "value");
        L2Value value = MakeNumber(0.0);
        if (type == "bool") {
          value = MakeBool(value_v && value_v->IsBool() ? value_v->AsBool() : false);
        } else if (type == "string") {
          value = MakeString(value_v && value_v->IsString() ? value_v->AsString() : "");
        } else {
          value = MakeNumber(value_v && value_v->IsNumber() ? value_v->AsNumber() : 0.0);
        }
        series.assign(static_cast<size_t>(row_count), value);
      } else if (kind == "bar_field") {
        const std::string field = JsonStringAt(node, "/field", "close");
        int lag = 0;
        const auto* lag_v = ObjectGet(node, "lag_bars");
        if (lag_v && lag_v->IsNumber()) {
          lag = std::max(0, static_cast<int>(std::llround(lag_v->AsNumber())));
        }
        for (int i = 0; i < row_count; ++i) {
          const int src = std::max(0, i - lag);
          if (field == "datetime") {
            series.push_back(MakeString(ToStdString(sc.DateTimeToString(snapshot.date_times[src], FLAG_DT_COMPLETE_DATETIME))));
          } else {
            series.push_back(MakeNumber(SnapshotBarFieldAsNumber(snapshot, src, field)));
          }
        }
      } else if (kind == "study_ref") {
        const std::string ref = JsonStringAt(node, "/ref", "");
        auto raw = BuildLayer2StudyRefSeries(
            sc,
            plan,
            snapshot,
            ref,
            symbol_sink,
            "/execution/layer2/indicator_dag/nodes/" + node_id + "/ref");
        if (raw.empty()) {
          symbol_ok = false;
          break;
        }
        int lag = 0;
        const auto* lag_v = ObjectGet(node, "lag_bars");
        if (lag_v && lag_v->IsNumber()) {
          lag = std::max(0, static_cast<int>(std::llround(lag_v->AsNumber())));
        }
        for (int i = 0; i < row_count; ++i) {
          const int src = std::max(0, i - lag);
          series.push_back(raw[static_cast<size_t>(src)]);
        }
      } else if (IsKnownLayer2Op(kind)) {
        const auto* inputs_v = ObjectGet(node, "inputs");
        const int window = std::max(1, static_cast<int>(std::llround(
                                           ObjectGet(node, "window_bars") && ObjectGet(node, "window_bars")->IsNumber()
                                               ? ObjectGet(node, "window_bars")->AsNumber()
                                               : 1.0)));
        std::vector<std::vector<L2Value>> inputs;
        if (inputs_v && inputs_v->IsArray()) {
          for (size_t i = 0; i < inputs_v->AsArray().items.size(); ++i) {
            const auto& dep = inputs_v->AsArray().items[i];
            if (!dep.IsString()) {
              continue;
            }
            auto dep_it = node_values.find(dep.AsString());
            if (dep_it == node_values.end()) {
              EmitRuntimeError(
                  symbol_sink,
                  "E_L2_MISSING_NODE_DEP",
                  "CHK_L2_NODE_DEP",
                  "Layer2 dependency was unavailable during evaluation.",
                  "evaluated dependency node",
                  dep.AsString(),
                  "/execution/layer2/indicator_dag/nodes/" + node_id + "/inputs/" + std::to_string(i),
                  "Ensure dependencies are valid and acyclic.");
              symbol_ok = false;
              break;
            }
            inputs.push_back(dep_it->second);
          }
        }
        if (!symbol_ok) {
          break;
        }

        if (kind == "ema") {
          const double alpha = 2.0 / (static_cast<double>(window) + 1.0);
          double prev = 0.0;
          for (int i = 0; i < row_count; ++i) {
            const double x = L2ToNumber(inputs[0][static_cast<size_t>(i)]);
            if (i == 0) {
              prev = x;
            } else {
              prev = alpha * x + (1.0 - alpha) * prev;
            }
            series.push_back(MakeNumber(prev));
          }
        } else if (kind == "atr") {
          for (int i = 0; i < row_count; ++i) {
            const int start = std::max(1, i - window + 1);
            double tr_sum = 0.0;
            int count = 0;
            for (int j = start; j <= i; ++j) {
              const double high = L2ToNumber(inputs[0][static_cast<size_t>(j)]);
              const double low = L2ToNumber(inputs.size() > 1 ? inputs[1][static_cast<size_t>(j)] : inputs[0][static_cast<size_t>(j)]);
              const double prev_close =
                  L2ToNumber(inputs.size() > 2 ? inputs[2][static_cast<size_t>(j - 1)] : inputs[0][static_cast<size_t>(j - 1)]);
              const double tr = std::max({high - low, std::fabs(high - prev_close), std::fabs(low - prev_close)});
              tr_sum += tr;
              ++count;
            }
            series.push_back(MakeNumber(count > 0 ? tr_sum / static_cast<double>(count) : 0.0));
          }
        } else {
          for (int i = 0; i < row_count; ++i) {
            series.push_back(EvaluateLayer2OpAt(kind, inputs, i, window));
          }
        }
      }
      node_values[node_id] = std::move(series);
    }

    if (!symbol_ok) {
      RelayDiagnostics(symbol_sink, sink, skip_symbol_failures, symbol);
      if (skip_symbol_failures) {
        EmitRuntimeWarning(
            sink,
            "E_RUN_SYMBOL_SKIPPED",
            "CHK_RUNTIME_SYMBOL_SKIPPED",
            "Symbol failed Layer2 node evaluation and was skipped due symbol_failure_policy=skip_symbol.",
            "symbol evaluated without runtime errors",
            symbol + (FirstErrorReason(symbol_sink).empty() ? "" : ", reason=" + FirstErrorReason(symbol_sink)),
            "/execution/layer2/symbol_failure_policy",
            "Set symbol_failure_policy=fail_fast to stop on first symbol failure.");
        continue;
      }
      result.ok = false;
      break;
    }

    std::vector<Layer2EventRow> events;
    const int evaluable_rows = require_final_bar ? std::max(0, row_count - 1) : row_count;
    bool prev_trigger = false;
    int last_emit_bar = -1000000;
    for (int i = 0; i < evaluable_rows; ++i) {
      const bool trigger = L2ToBool(node_values[trigger_node][static_cast<size_t>(i)]);
      const std::string bar_datetime = ToStdString(sc.DateTimeToString(snapshot.date_times[i], FLAG_DT_COMPLETE_DATETIME));
      if (!DateInRange(bar_datetime, date_range)) {
        prev_trigger = trigger;
        continue;
      }
      bool fire = (emit_mode == "on_true") ? trigger : (trigger && !prev_trigger);
      if (fire && cooldown > 0 && (i - last_emit_bar) <= cooldown) {
        fire = false;
      }
      if (fire) {
        Layer2EventRow event;
        event.symbol = symbol;
        event.bar_index = i;
        event.columns["symbol"] = symbol;
        event.columns["bar_index"] = std::to_string(i);
        for (size_t c = 0; c < emit_columns->AsArray().items.size(); ++c) {
          const auto& col = emit_columns->AsArray().items[c];
          if (!col.IsObject()) {
            continue;
          }
          const std::string name = JsonStringAt(col, "/name", "");
          if (name.empty()) {
            continue;
          }
          const std::string type = JsonStringAt(col, "/type", "string");
          const std::string ref = JsonStringAt(col, "/ref", "");
          const std::string node_ref = JsonStringAt(col, "/node", "");
          if (ref.empty() && node_ref.empty()) {
            EmitRuntimeError(
                symbol_sink,
                "E_L2_MISSING_NODE_DEP",
                "CHK_L2_EMIT_COLUMN_BINDING",
                "Layer2 emit column must declare either ref or node.",
                "ref or node",
                "missing",
                "/execution/layer2/event_emitter/emit_columns/" + std::to_string(c),
                "Set ref or node for the emit column.");
            symbol_ok = false;
            break;
          }

          std::string raw;
          if (!ref.empty()) {
            if (ref == "@symbol") {
              raw = symbol;
            } else if (ref == "@bar.datetime") {
              raw = bar_datetime;
            } else if (ref == "@bar.open" || ref == "@bar.high" || ref == "@bar.low" || ref == "@bar.close" ||
                       ref == "@bar.volume" || ref == "@bar.vwap") {
              raw = FormatNumber(SnapshotBarFieldAsNumber(snapshot, i, ref.substr(5)));
            } else if (ref.rfind("@study.", 0) == 0) {
              DatasetFieldSpec temp{name, ref};
              std::string resolved;
              if (ResolveDatasetFieldValue(sc, plan, temp, snapshot, i, resolved, symbol_sink)) {
                raw = std::move(resolved);
              }
            }
          }

          if (raw.empty() && !node_ref.empty()) {
            auto node_it = node_values.find(node_ref);
            if (node_it == node_values.end() || i >= static_cast<int>(node_it->second.size())) {
              EmitRuntimeError(
                  symbol_sink,
                  "E_L2_MISSING_NODE_DEP",
                  "CHK_L2_EMIT_NODE",
                  "Layer2 emit column references a missing node.",
                  "existing node id",
                  node_ref,
                  "/execution/layer2/event_emitter/emit_columns/" + std::to_string(c) + "/node",
                  "Reference an existing indicator_dag node.");
              symbol_ok = false;
              break;
            }
            raw = L2ToString(node_it->second[static_cast<size_t>(i)]);
          }
          event.columns[name] = CoerceEmitType(raw, type);
        }
        if (!symbol_ok) {
          break;
        }
        events.push_back(std::move(event));
        last_emit_bar = i;
      }
      prev_trigger = trigger;
    }
    if (!symbol_ok) {
      RelayDiagnostics(symbol_sink, sink, skip_symbol_failures, symbol);
      if (skip_symbol_failures) {
        EmitRuntimeWarning(
            sink,
            "E_RUN_SYMBOL_SKIPPED",
            "CHK_RUNTIME_SYMBOL_SKIPPED",
            "Symbol failed Layer2 evaluation and was skipped due symbol_failure_policy=skip_symbol.",
            "symbol evaluated without runtime errors",
            symbol + (FirstErrorReason(symbol_sink).empty() ? "" : ", reason=" + FirstErrorReason(symbol_sink)),
            "/execution/layer2/symbol_failure_policy",
            "Set symbol_failure_policy=fail_fast to stop on first symbol failure.");
        continue;
      }
      result.ok = false;
      break;
    }

    RelayDiagnostics(symbol_sink, sink, false, symbol);
    result.accessible_symbols.push_back(symbol);
    result.snapshots_by_symbol[symbol] = snapshot;
    result.events_by_symbol[symbol] = std::move(events);
  }

  if (result.accessible_symbols.empty()) {
    EmitRuntimeError(
        sink,
        "E_RUN_DATA_UNAVAILABLE",
        "CHK_RUNTIME_NO_ACCESSIBLE_SYMBOLS",
        "No symbols from universe were accessible for Layer2 evaluation.",
        "at least one accessible universe symbol",
        "none accessible",
        "/universe/symbols",
        "Open at least one universe symbol chart, or adjust universe.symbols.");
    result.ok = false;
  }
  return result;
}

double QuantileValue(std::vector<double> values, double q) {
  if (values.empty()) {
    return 0.0;
  }
  std::sort(values.begin(), values.end());
  q = std::max(0.0, std::min(1.0, q));
  const double pos = q * static_cast<double>(values.size() - 1);
  const size_t lo = static_cast<size_t>(std::floor(pos));
  const size_t hi = static_cast<size_t>(std::ceil(pos));
  if (lo >= values.size()) {
    return values.back();
  }
  if (hi >= values.size() || lo == hi) {
    return values[lo];
  }
  const double alpha = pos - static_cast<double>(lo);
  return values[lo] * (1.0 - alpha) + values[hi] * alpha;
}


void ParseHeadersToIndex(const std::vector<std::string>& headers, std::unordered_map<std::string, size_t>& index) {
  for (size_t i = 0; i < headers.size(); ++i) {
    index[headers[i]] = i;
  }
}

bool ParseQuantiles(
    const sre::JsonValue& plan,
    std::string_view pointer,
    std::vector<double>& out,
    sre::layers::DiagnosticSink& sink) {
  const auto* value = sre::ResolvePointer(plan, pointer);
  if (!value || !value->IsArray()) {
    EmitRuntimeError(
        sink,
        "E_NOT_IMPLEMENTED",
        "CHK_RUNTIME_LAYER3_QUANTILES_NOT_IMPLEMENTED",
        "Layer3 quantiles list is missing or invalid.",
        "array of numbers",
        "missing",
        std::string(pointer));
    return false;
  }
  for (const auto& item : value->AsArray().items) {
    if (!item.IsNumber()) {
      EmitRuntimeError(
          sink,
          "E_NOT_IMPLEMENTED",
          "CHK_RUNTIME_LAYER3_QUANTILES_NOT_IMPLEMENTED",
          "Layer3 quantile entry is not numeric.",
          "numeric quantile in (0,1]",
          item.TypeName(),
          std::string(pointer));
      return false;
    }
    const double q = item.AsNumber();
    if (!(q > 0.0 && q <= 1.0)) {
      EmitRuntimeError(
          sink,
          "E_NOT_IMPLEMENTED",
          "CHK_RUNTIME_LAYER3_QUANTILES_NOT_IMPLEMENTED",
          "Layer3 quantile entry is outside supported range.",
          "0 < q <= 1",
          FormatNumber(q),
          std::string(pointer));
      return false;
    }
    out.push_back(q);
  }
  return !out.empty();
}

std::string QuantileFieldName(double q, std::string_view kind) {
  const int quantile = static_cast<int>(std::round(q * 100.0));
  return "p" + std::to_string(quantile) + "_" + std::string(kind) + "_price";
}

bool EvaluateSimpleRule(
    const std::unordered_map<std::string, size_t>& header_index,
    const std::vector<std::string>& row,
    const sre::JsonValue& rule,
    bool is_row_rule,
    double rr_price,
    bool& out_pass,
    std::string& out_rule_id,
    std::string& out_reason,
    sre::layers::DiagnosticSink& sink,
    std::string pointer_prefix) {
  out_rule_id = "rule";
  out_reason = "ok";
  out_pass = false;
  if (!rule.IsObject()) {
    EmitRuntimeError(
        sink,
        "E_NOT_IMPLEMENTED",
        "CHK_RUNTIME_LAYER3_RULE_EVAL_NOT_IMPLEMENTED",
        "Layer3 rule entry must be an object.",
        "rule object",
        rule.TypeName(),
        pointer_prefix);
    return false;
  }
  const auto* id = ObjectGet(rule, "id");
  const auto* field = ObjectGet(rule, "field");
  const auto* op = ObjectGet(rule, "op");
  const auto* value = ObjectGet(rule, "value");
  if (id && id->IsString() && !id->AsString().empty()) {
    out_rule_id = id->AsString();
  }
  if (!field || !field->IsString() || !op || !op->IsString()) {
    EmitRuntimeError(
        sink,
        "E_NOT_IMPLEMENTED",
        "CHK_RUNTIME_LAYER3_RULE_EVAL_NOT_IMPLEMENTED",
        "Layer3 rule requires string fields: field/op.",
        "field+op strings",
        "missing",
        pointer_prefix);
    return false;
  }

  const std::string field_name = field->AsString();
  const std::string op_text = op->AsString();
  std::string field_value;
  bool field_found = false;

  if (is_row_rule && field_name == "rr_price") {
    field_value = FormatNumber(rr_price);
    field_found = true;
  } else {
    auto it = header_index.find(field_name);
    if (it != header_index.end() && it->second < row.size()) {
      field_value = row[it->second];
      field_found = true;
    }
  }

  if (!field_found) {
    out_pass = false;
    out_reason = "missing field: " + field_name;
    EmitRuntimeError(
        sink,
        "E_NOT_IMPLEMENTED",
        "CHK_RUNTIME_LAYER3_SOURCE_COLUMNS_NOT_IMPLEMENTED",
        "Layer3 rule references a field not present in Layer2 input CSV.",
        "field present in layer2_authoritative_csv",
        field_name,
        pointer_prefix + "/field");
    return false;
  }

  if (op_text == "not_null") {
    out_pass = !field_value.empty();
    out_reason = out_pass ? "field present" : "field empty";
    return true;
  }

  if (op_text == ">=" || op_text == ">") {
    if (!value || !value->IsNumber()) {
      EmitRuntimeError(
          sink,
          "E_NOT_IMPLEMENTED",
          "CHK_RUNTIME_LAYER3_RULE_EVAL_NOT_IMPLEMENTED",
          "Comparison rules require numeric value.",
          "numeric value",
          value ? value->TypeName() : "missing",
          pointer_prefix + "/value");
      return false;
    }
    double lhs = 0.0;
    if (!ParseDouble(field_value, lhs)) {
      EmitRuntimeError(
          sink,
          "E_NOT_IMPLEMENTED",
          "CHK_RUNTIME_LAYER3_RULE_EVAL_NOT_IMPLEMENTED",
          "Layer3 rule field value is not numeric.",
          "numeric field",
          field_value,
          pointer_prefix + "/field");
      return false;
    }
    const double rhs = value->AsNumber();
    out_pass = (op_text == ">=") ? (lhs >= rhs) : (lhs > rhs);
    out_reason = "lhs=" + FormatNumber(lhs) + " op " + op_text + " rhs=" + FormatNumber(rhs);
    return true;
  }

  EmitRuntimeError(
      sink,
      "E_NOT_IMPLEMENTED",
      "CHK_RUNTIME_LAYER3_RULE_OP_NOT_IMPLEMENTED",
      "Layer3 rule operation is not implemented.",
      "not_null|>=|>",
      op_text,
      pointer_prefix + "/op");
  return false;
}

bool EmitDatasetArtifacts(
    SCStudyInterfaceRef sc,
    const sre::JsonValue& plan,
    const std::filesystem::path& repo_root,
    const std::vector<std::string>& symbols,
    std::vector<std::string>& emitted_symbols,
    sre::layers::DiagnosticSink& sink) {
  const auto* format_v = sre::ResolvePointer(plan, "/outputs/dataset/format");
  const auto* path_v = sre::ResolvePointer(plan, "/outputs/dataset/path");
  if (!format_v || !format_v->IsString() || !path_v || !path_v->IsString() || path_v->AsString().empty()) {
    return true;
  }

  const std::string dataset_format = format_v->AsString();
  const std::string raw_path = path_v->AsString();
  const auto fields = ParseDatasetFields(plan);
  if (fields.empty()) {
    return true;
  }

  if (dataset_format != "csv" && dataset_format != "jsonl") {
    EmitRuntimeError(
        sink,
        "E_NOT_IMPLEMENTED",
        "CHK_RUNTIME_DATASET_FORMAT_NOT_IMPLEMENTED",
        "Dataset format is unsupported by ACSIL runtime emitter.",
        "csv|jsonl",
        dataset_format,
        "/outputs/dataset/format");
    return false;
  }

  std::vector<std::string> headers;
  headers.reserve(fields.size());
  for (const auto& field : fields) {
    headers.push_back(field.name);
  }

  const auto write_rows = [&](const std::string& path_template,
                              const std::unordered_map<std::string, std::vector<std::vector<std::string>>>& rows_by_symbol) {
    const bool per_symbol = path_template.find("{symbol}") != std::string::npos;
    if (per_symbol) {
      for (const auto& [symbol, rows] : rows_by_symbol) {
        const auto out_path = ResolveOutputPath(path_template, repo_root, symbol);
        if (dataset_format == "csv") {
          WriteCsvFile(out_path, headers, rows);
        } else {
          WriteJsonlFile(out_path, headers, rows);
        }
      }
      return;
    }
    std::vector<std::vector<std::string>> merged;
    for (const auto& [_, rows] : rows_by_symbol) {
      merged.insert(merged.end(), rows.begin(), rows.end());
    }
    const auto out_path = ResolveOutputPath(path_template, repo_root, "");
    if (dataset_format == "csv") {
      WriteCsvFile(out_path, headers, merged);
    } else {
      WriteJsonlFile(out_path, headers, merged);
    }
  };

  const std::string dataset_source = DatasetSourceFromPlan(plan);
  if (dataset_source == "bars") {
    // Preserve legacy behavior: emit one row per accessible symbol (latest bar only).
    std::unordered_map<std::string, std::vector<std::vector<std::string>>> rows_by_symbol;
    bool ok = true;
    const PlanDateRange date_range = ParsePlanDateRange(plan);
    for (const auto& symbol : symbols) {
      SymbolSnapshot snapshot;
      if (!LoadSymbolSnapshot(sc, plan, symbol, snapshot, sink)) {
        continue;
      }
      const std::string bar_datetime =
          ToStdString(sc.DateTimeToString(snapshot.date_times[snapshot.bar_index], FLAG_DT_COMPLETE_DATETIME));
      if (!DateInRange(bar_datetime, date_range)) {
        continue;
      }
      std::vector<std::string> row;
      row.reserve(fields.size());
      bool symbol_ok = true;
      for (const auto& field : fields) {
        std::string value;
        if (!ResolveDatasetFieldValue(sc, plan, field, snapshot, snapshot.bar_index, value, sink)) {
          symbol_ok = false;
          ok = false;
          break;
        }
        row.push_back(std::move(value));
      }
      if (!symbol_ok) {
        continue;
      }
      rows_by_symbol[symbol].push_back(std::move(row));
      emitted_symbols.push_back(symbol);
    }
    if (emitted_symbols.empty()) {
      EmitRuntimeError(
          sink,
          "E_RUN_DATA_UNAVAILABLE",
          "CHK_RUNTIME_NO_ACCESSIBLE_SYMBOLS",
          "No symbols from universe were accessible for dataset emission.",
          "at least one accessible universe symbol",
          "none accessible",
          "/universe/symbols",
          "Open at least one universe symbol chart, or adjust universe.symbols.");
      return false;
    }
    write_rows(raw_path, rows_by_symbol);
    return ok;
  }

  if (dataset_source == "layer2_event_emitter") {
    Layer2EvaluationResult l2 = EvaluateLayer2(sc, plan, symbols, sink);
    if (!l2.ok) {
      return false;
    }

    const PlanDateRange date_range = ParsePlanDateRange(plan);
    const bool enforce_l2_date_range = JsonBoolAt(plan, "/execution/layer2/enabled", false) && date_range.enabled;
    std::string emitted_min_dt = "9999-99-99";
    std::string emitted_max_dt = "0000-00-00";
    auto observe_dt = [&](std::string_view dt) {
      if (dt.size() < 10) {
        return;
      }
      const std::string d(dt.substr(0, 10));
      emitted_min_dt = std::min(emitted_min_dt, d);
      emitted_max_dt = std::max(emitted_max_dt, d);
    };

    std::unordered_map<std::string, std::vector<std::vector<std::string>>> rows_by_symbol;
    for (const auto& symbol : l2.accessible_symbols) {
      auto snap_it = l2.snapshots_by_symbol.find(symbol);
      if (snap_it == l2.snapshots_by_symbol.end()) {
        continue;
      }
      const SymbolSnapshot& snapshot = snap_it->second;
      const auto events_it = l2.events_by_symbol.find(symbol);
      const auto& events = (events_it == l2.events_by_symbol.end()) ? std::vector<Layer2EventRow>{} : events_it->second;
      std::vector<std::vector<std::string>> rows;
      rows.reserve(events.size());
      for (const auto& event : events) {
        std::string event_dt;
        auto dt_it = event.columns.find("bar_datetime");
        if (dt_it != event.columns.end() && !dt_it->second.empty()) {
          event_dt = dt_it->second;
        } else if (event.bar_index >= 0 && event.bar_index < snapshot.date_times.GetArraySize()) {
          event_dt = ToStdString(sc.DateTimeToString(snapshot.date_times[event.bar_index], FLAG_DT_COMPLETE_DATETIME));
        }
        if (enforce_l2_date_range) {
          observe_dt(event_dt);
          if (!DateInRange(event_dt, date_range)) {
            sre::layers::EmitLayerError(
                sink,
                "outputs_repro",
                "E_L2_DATE_RANGE_VIOLATION",
                "CHK_DATE_RANGE_ENFORCED_IN_L2",
                "CHKGRP_PLAN_AST_SHAPE",
                "L2 authoritative emission produced a row outside universe.date_range.",
                "rows constrained to date_range.start..date_range.end",
                "date_range.start=" + date_range.start_date + ", date_range.end=" + date_range.end_date + ", emitted_min_dt=" +
                    emitted_min_dt + ", emitted_max_dt=" + emitted_max_dt + ", violating_dt=" + event_dt,
                {"/universe/date_range", "/execution/layer2"},
                "Fix L2 emitter to filter bars by universe.date_range",
                "acsil_runtime");
            return false;
          }
        }

        std::vector<std::string> row;
        row.reserve(fields.size());
        bool row_ok = true;
        for (const auto& field : fields) {
          std::string value;
          const std::string& source = field.source;
          if (source.empty()) {
            auto it = event.columns.find(field.name);
            value = (it == event.columns.end()) ? "" : it->second;
          } else if (source[0] != '@') {
            value = source;
          } else if (source.rfind("@event.", 0) == 0) {
            const std::string key = source.substr(7);
            auto it = event.columns.find(key);
            value = (it == event.columns.end()) ? "" : it->second;
          } else if (source == "@symbol") {
            value = symbol;
          } else {
            DatasetFieldSpec temp{field.name, source};
            if (!ResolveDatasetFieldValue(sc, plan, temp, snapshot, event.bar_index, value, sink)) {
              row_ok = false;
              break;
            }
          }
          row.push_back(std::move(value));
        }
        if (row_ok) {
          rows.push_back(std::move(row));
        }
      }
      rows_by_symbol[symbol] = std::move(rows);
      emitted_symbols.push_back(symbol);
    }
    if (emitted_symbols.empty()) {
      EmitRuntimeError(
          sink,
          "E_RUN_DATA_UNAVAILABLE",
          "CHK_RUNTIME_NO_ACCESSIBLE_SYMBOLS",
          "No symbols from universe were accessible for Layer2 event emission.",
          "at least one accessible universe symbol",
          "none accessible",
          "/universe/symbols",
          "Open at least one universe symbol chart, or adjust universe.symbols.");
      return false;
    }
    write_rows(raw_path, rows_by_symbol);
    return true;
  }

  EmitRuntimeError(
      sink,
      "E_NOT_IMPLEMENTED",
      "CHK_RUNTIME_DATASET_SOURCE_NOT_IMPLEMENTED",
      "outputs.dataset.source is not supported by ACSIL runtime.",
      "bars|layer2_event_emitter",
      dataset_source,
      "/outputs/dataset/source",
      "Set outputs.dataset.source to bars or layer2_event_emitter.");
  return false;
}

bool EmitLayer3RrMenuArtifacts(
    const sre::JsonValue& plan,
    const std::filesystem::path& repo_root,
    const std::vector<std::string>& symbols,
    sre::layers::DiagnosticSink& sink) {
  const auto* layer3 = sre::ResolvePointer(plan, "/outputs/layer3");
  if (!layer3 || !layer3->IsObject()) {
    return true;
  }
  const auto* enabled = ObjectGet(*layer3, "enabled");
  const bool layer3_enabled = (enabled == nullptr) || !enabled->IsBool() || enabled->AsBool();
  if (!layer3_enabled) {
    return true;
  }

  const std::string input_path_template =
      JsonStringAt(plan, "/outputs/layer3/inputs/layer2_authoritative_csv", std::string());
  const std::string rr_path_template = JsonStringAt(plan, "/outputs/layer3/artifacts/rr_buckets", std::string());
  const std::string audit_path_template = JsonStringAt(plan, "/outputs/layer3/artifacts/decision_audit", std::string());
  if (input_path_template.empty() || rr_path_template.empty() || audit_path_template.empty()) {
    EmitRuntimeError(
        sink,
        "E_NOT_IMPLEMENTED",
        "CHK_RUNTIME_LAYER3_IO_PATHS_NOT_IMPLEMENTED",
        "Layer3 input/artifact paths must be fully specified.",
        "non-empty inputs.layer2_authoritative_csv and artifacts paths",
        "missing path",
        "/outputs/layer3");
    return false;
  }

  std::vector<double> risk_quantiles;
  std::vector<double> reward_quantiles;
  bool ok = ParseQuantiles(plan, "/outputs/layer3/rr_menu/risk_quantiles", risk_quantiles, sink);
  ok = ParseQuantiles(plan, "/outputs/layer3/rr_menu/reward_quantiles", reward_quantiles, sink) && ok;
  if (!ok) {
    return false;
  }

  const std::string entry_price_field =
      JsonStringAt(plan, "/outputs/layer3/normalization/entry_price_field", "avg_entry_price");

  const auto* eligibility_rules = sre::ResolvePointer(plan, "/outputs/layer3/eligibility/rules");
  const auto* row_rules = sre::ResolvePointer(plan, "/outputs/layer3/rr_menu/row_rules");

  bool all_ok = true;
  for (const auto& symbol : symbols) {
    const auto input_csv = ResolveOutputPath(input_path_template, repo_root, symbol);
    std::vector<std::string> headers;
    std::vector<std::vector<std::string>> rows;
    if (!ReadCsvFile(input_csv, headers, rows) || rows.empty()) {
      EmitRuntimeError(
          sink,
          "E_RUN_DATA_UNAVAILABLE",
          "CHK_RUNTIME_LAYER3_INPUT_CSV_MISSING",
          "Layer3 input CSV is missing or empty for symbol.",
          "non-empty layer2_authoritative_csv",
          input_csv.string(),
          "/outputs/layer3/inputs/layer2_authoritative_csv",
          "Ensure layer2 dataset output was written before layer3.");
      all_ok = false;
      continue;
    }

    std::unordered_map<std::string, size_t> header_index;
    ParseHeadersToIndex(headers, header_index);
    const auto& row = rows.back();

    std::vector<std::vector<std::string>> audit_rows;
    bool eligible = true;

    if (header_index.find(entry_price_field) == header_index.end()) {
      EmitRuntimeError(
          sink,
          "E_NOT_IMPLEMENTED",
          "CHK_RUNTIME_LAYER3_SOURCE_COLUMNS_NOT_IMPLEMENTED",
          "Layer3 normalization entry_price_field is not available in input CSV.",
          "field present in layer2_authoritative_csv",
          entry_price_field,
          "/outputs/layer3/normalization/entry_price_field");
      eligible = false;
      all_ok = false;
    }

    if (eligibility_rules && eligibility_rules->IsArray()) {
      for (size_t i = 0; i < eligibility_rules->AsArray().items.size(); ++i) {
        const auto& rule = eligibility_rules->AsArray().items[i];
        bool pass = false;
        std::string rule_id;
        std::string reason;
        const bool evaluated = EvaluateSimpleRule(
            header_index,
            row,
            rule,
            false,
            0.0,
            pass,
            rule_id,
            reason,
            sink,
            "/outputs/layer3/eligibility/rules/" + std::to_string(i));
        if (!evaluated) {
          eligible = false;
          all_ok = false;
        } else if (!pass) {
          eligible = false;
        }
        audit_rows.push_back({symbol, rule_id, pass ? "pass" : "fail", reason, input_csv.string()});
      }
    }

    std::vector<std::vector<std::string>> rr_rows;
    if (eligible) {
      for (double risk_q : risk_quantiles) {
        const std::string risk_field = QuantileFieldName(risk_q, "mae");
        auto risk_it = header_index.find(risk_field);
        if (risk_it == header_index.end() || risk_it->second >= row.size()) {
          EmitRuntimeError(
              sink,
              "E_NOT_IMPLEMENTED",
              "CHK_RUNTIME_LAYER3_SOURCE_COLUMNS_NOT_IMPLEMENTED",
              "Layer3 rr_menu requires MAE quantile column in input CSV.",
              "column present: " + risk_field,
              "missing",
              "/outputs/layer3/rr_menu/risk_quantiles");
          all_ok = false;
          eligible = false;
          break;
        }

        double risk_value = 0.0;
        if (!ParseDouble(row[risk_it->second], risk_value) || risk_value <= 0.0) {
          EmitRuntimeError(
              sink,
              "E_NOT_IMPLEMENTED",
              "CHK_RUNTIME_LAYER3_SOURCE_COLUMNS_NOT_IMPLEMENTED",
              "MAE quantile column is non-numeric or non-positive.",
              "positive numeric value",
              row[risk_it->second],
              "/outputs/layer3/rr_menu/risk_quantiles");
          all_ok = false;
          eligible = false;
          break;
        }

        for (double reward_q : reward_quantiles) {
          const std::string reward_field = QuantileFieldName(reward_q, "mfe");
          auto reward_it = header_index.find(reward_field);
          if (reward_it == header_index.end() || reward_it->second >= row.size()) {
            EmitRuntimeError(
                sink,
                "E_NOT_IMPLEMENTED",
                "CHK_RUNTIME_LAYER3_SOURCE_COLUMNS_NOT_IMPLEMENTED",
                "Layer3 rr_menu requires MFE quantile column in input CSV.",
                "column present: " + reward_field,
                "missing",
                "/outputs/layer3/rr_menu/reward_quantiles");
            all_ok = false;
            eligible = false;
            break;
          }
          double reward_value = 0.0;
          if (!ParseDouble(row[reward_it->second], reward_value)) {
            EmitRuntimeError(
                sink,
                "E_NOT_IMPLEMENTED",
                "CHK_RUNTIME_LAYER3_SOURCE_COLUMNS_NOT_IMPLEMENTED",
                "MFE quantile column is non-numeric.",
                "numeric value",
                row[reward_it->second],
                "/outputs/layer3/rr_menu/reward_quantiles");
            all_ok = false;
            eligible = false;
            break;
          }

          const double rr_price = reward_value / risk_value;
          bool rr_eligible = true;
          if (row_rules && row_rules->IsArray()) {
            for (size_t k = 0; k < row_rules->AsArray().items.size(); ++k) {
              bool row_pass = false;
              std::string row_rule_id;
              std::string row_reason;
              const bool evaluated = EvaluateSimpleRule(
                  header_index,
                  row,
                  row_rules->AsArray().items[k],
                  true,
                  rr_price,
                  row_pass,
                  row_rule_id,
                  row_reason,
                  sink,
                  "/outputs/layer3/rr_menu/row_rules/" + std::to_string(k));
              if (!evaluated) {
                rr_eligible = false;
                all_ok = false;
              } else if (!row_pass) {
                rr_eligible = false;
              }
            }
          }

          std::string bucket_id = "default";
          auto bucket_it = header_index.find("bucket_id");
          if (bucket_it != header_index.end() && bucket_it->second < row.size() && !row[bucket_it->second].empty()) {
            bucket_id = row[bucket_it->second];
          }
          rr_rows.push_back(
              {symbol,
               bucket_id,
               FormatNumber(risk_q),
               FormatNumber(reward_q),
               FormatNumber(rr_price),
               rr_eligible ? "1" : "0",
               input_csv.string()});
        }
      }
    }

    WriteCsvFile(
        ResolveOutputPath(audit_path_template, repo_root, symbol),
        {"symbol", "rule_id", "result", "reason", "input_csv"},
        audit_rows);

    WriteCsvFile(
        ResolveOutputPath(rr_path_template, repo_root, symbol),
        {"symbol", "bucket_id", "risk_q", "reward_q", "rr_price", "eligible", "input_csv"},
        rr_rows);
  }

  return all_ok;
}

bool Layer3BucketEvalRequested(const sre::JsonValue& plan) {
  const std::string mode = JsonStringAt(plan, "/outputs/layer3/mode", "rr_menu");
  if (mode != "bucket_eval") {
    return false;
  }
  if (!JsonBoolAt(plan, "/outputs/layer3/outcomes/enabled", false)) {
    return false;
  }
  const auto* dims = sre::ResolvePointer(plan, "/outputs/layer3/bucketing/dimensions");
  return dims && dims->IsArray() && !dims->AsArray().items.empty();
}

bool EvaluateBucketRule(
    const sre::JsonValue& rule,
    const std::unordered_map<std::string, std::string>& fields,
    bool& pass,
    std::string& reason) {
  pass = false;
  reason = "invalid_rule";
  if (!rule.IsObject()) {
    return false;
  }
  const auto* scope_v = ObjectGet(rule, "scope");
  if (!scope_v || !scope_v->IsString() || scope_v->AsString() != "bucket") {
    pass = true;
    reason = "scope_skipped";
    return true;
  }
  const auto* field_v = ObjectGet(rule, "field");
  const auto* op_v = ObjectGet(rule, "op");
  const auto* value_v = ObjectGet(rule, "value");
  if (!field_v || !field_v->IsString() || !op_v || !op_v->IsString()) {
    return false;
  }
  const std::string field = field_v->AsString();
  const std::string op = op_v->AsString();

  auto it = fields.find(field);
  const bool has_field = it != fields.end() && !it->second.empty();
  const std::string lhs_text = has_field ? it->second : "";

  if (op == "not_null") {
    pass = has_field;
    reason = pass ? "field present" : "field missing";
    return true;
  }
  if (op == "is_null") {
    pass = !has_field;
    reason = pass ? "field missing" : "field present";
    return true;
  }

  if (!has_field) {
    pass = false;
    reason = "missing field: " + field;
    return true;
  }

  if (op == "==" || op == "!=") {
    if (value_v && value_v->IsString()) {
      pass = (op == "==") ? (lhs_text == value_v->AsString()) : (lhs_text != value_v->AsString());
      reason = "string compare";
      return true;
    }
    if (value_v && value_v->IsNumber()) {
      double lhs = 0.0;
      if (!ParseDouble(lhs_text, lhs)) {
        pass = false;
        reason = "lhs not numeric";
        return true;
      }
      const double rhs = value_v->AsNumber();
      pass = (op == "==") ? (std::fabs(lhs - rhs) < 1e-12) : (std::fabs(lhs - rhs) >= 1e-12);
      reason = "numeric compare";
      return true;
    }
    return false;
  }

  if (!(op == ">" || op == ">=" || op == "<" || op == "<=")) {
    return false;
  }
  if (!value_v || !value_v->IsNumber()) {
    return false;
  }
  double lhs = 0.0;
  if (!ParseDouble(lhs_text, lhs)) {
    pass = false;
    reason = "lhs not numeric";
    return true;
  }
  const double rhs = value_v->AsNumber();
  if (op == ">") pass = lhs > rhs;
  if (op == ">=") pass = lhs >= rhs;
  if (op == "<") pass = lhs < rhs;
  if (op == "<=") pass = lhs <= rhs;
  reason = "lhs=" + FormatNumber(lhs) + " op " + op + " rhs=" + FormatNumber(rhs);
  return true;
}

bool EmitLayer3BucketEvalArtifacts(
    SCStudyInterfaceRef sc,
    const sre::JsonValue& plan,
    const std::filesystem::path& repo_root,
    const std::vector<std::string>& symbols,
    sre::layers::DiagnosticSink& sink) {
  const std::string input_template = JsonStringAt(plan, "/outputs/layer3/inputs/layer2_authoritative_csv", "");
  if (input_template.empty()) {
    EmitRuntimeError(
        sink,
        "E_NOT_IMPLEMENTED",
        "CHK_RUNTIME_LAYER3_IO_PATHS_NOT_IMPLEMENTED",
        "Layer3 bucket_eval requires inputs.layer2_authoritative_csv.",
        "non-empty inputs.layer2_authoritative_csv",
        "missing",
        "/outputs/layer3/inputs/layer2_authoritative_csv");
    return false;
  }

  const auto leakage_kind = [&](std::string_view pointer) -> bool {
    const auto* kind_v = sre::ResolvePointer(plan, pointer);
    if (!kind_v || !kind_v->IsString()) {
      return false;
    }
    const std::string kind = kind_v->AsString();
    return kind.find("same_bar") != std::string::npos || kind.find("from_t") != std::string::npos ||
           kind.find("include_t") != std::string::npos;
  };
  if (leakage_kind("/outputs/layer3/outcomes/compute/mfe/kind") || leakage_kind("/outputs/layer3/outcomes/compute/mfa/kind") ||
      leakage_kind("/outputs/layer3/outcomes/compute/ret/kind")) {
    EmitRuntimeError(
        sink,
        "E_L3_OUTCOME_LEAKAGE_SAME_BAR",
        "CHK_L3_OUTCOME_LEAKAGE",
        "Layer3 outcomes compute config would include same-bar data (t).",
        "t+1..t+H only",
        "same_bar compute kind",
        "/outputs/layer3/outcomes/compute",
        "Use compute kinds that start from t+1.");
    return false;
  }

  std::vector<int> horizons;
  bool emit_horizon_column = false;
  const auto* horizons_v = sre::ResolvePointer(plan, "/outputs/layer3/outcomes/horizons_bars");
  if (horizons_v && horizons_v->IsArray() && !horizons_v->AsArray().items.empty()) {
    emit_horizon_column = true;
    for (const auto& item : horizons_v->AsArray().items) {
      if (!item.IsNumber()) {
        continue;
      }
      const int h = static_cast<int>(std::llround(item.AsNumber()));
      if (h > 0) {
        horizons.push_back(h);
      }
    }
    std::sort(horizons.begin(), horizons.end());
    horizons.erase(std::unique(horizons.begin(), horizons.end()), horizons.end());
  }
  if (horizons.empty()) {
    const int horizon = std::max(1, static_cast<int>(std::llround(
                                         sre::ResolvePointer(plan, "/outputs/layer3/outcomes/horizon/value") &&
                                                 sre::ResolvePointer(plan, "/outputs/layer3/outcomes/horizon/value")->IsNumber()
                                             ? sre::ResolvePointer(plan, "/outputs/layer3/outcomes/horizon/value")->AsNumber()
                                             : 1.0)));
    horizons.push_back(horizon);
  }

  std::vector<std::string> sides = {"long"};
  bool emit_side_column = false;
  const auto* sides_v = sre::ResolvePointer(plan, "/outputs/layer3/outcomes/sides");
  if (sides_v && sides_v->IsArray() && !sides_v->AsArray().items.empty()) {
    std::vector<std::string> parsed;
    for (const auto& item : sides_v->AsArray().items) {
      if (!item.IsString()) {
        continue;
      }
      const std::string side = item.AsString();
      if ((side == "long" || side == "short") && std::find(parsed.begin(), parsed.end(), side) == parsed.end()) {
        parsed.push_back(side);
      }
    }
    if (!parsed.empty()) {
      sides = std::move(parsed);
    }
  }
  emit_side_column = sides.size() > 1;

  const std::string entry_field = JsonStringAt(plan, "/outputs/layer3/outcomes/entry_price_field", "close");
  const std::string high_field = JsonStringAt(plan, "/outputs/layer3/outcomes/price_fields/high", "high");
  const std::string low_field = JsonStringAt(plan, "/outputs/layer3/outcomes/price_fields/low", "low");
  const std::string close_field = JsonStringAt(plan, "/outputs/layer3/outcomes/price_fields/close", "close");
  const std::string cost_type = JsonStringAt(plan, "/outputs/layer3/outcomes/cost_model/type", "none");
  const double bps = sre::ResolvePointer(plan, "/outputs/layer3/outcomes/cost_model/bps") &&
                             sre::ResolvePointer(plan, "/outputs/layer3/outcomes/cost_model/bps")->IsNumber()
                         ? std::max(0.0, sre::ResolvePointer(plan, "/outputs/layer3/outcomes/cost_model/bps")->AsNumber())
                         : 0.0;

  struct OutcomeRow {
    std::string symbol;
    int event_index = 0;
    int event_bar_index = 0;
    int horizon_bars = 0;
    std::string side = "long";
    double entry_price = 0.0;
    double mfe_pct = 0.0;
    double mfa_pct = 0.0;
    double ret_pct = 0.0;
    double net_ret_pct = 0.0;
    std::unordered_map<std::string, double> numeric_fields;
    std::unordered_map<std::string, int> dim_bucket;
    std::unordered_map<std::string, double> dim_lo;
    std::unordered_map<std::string, double> dim_hi;
    std::string bucket_key = "all";
  };

  std::vector<OutcomeRow> outcomes;
  for (const auto& symbol : symbols) {
    SymbolSnapshot snapshot;
    if (!LoadSymbolSnapshot(sc, plan, symbol, snapshot, sink)) {
      continue;
    }

    const auto input_csv = ResolveOutputPath(input_template, repo_root, symbol);
    std::vector<std::string> headers;
    std::vector<std::vector<std::string>> rows;
    if (!ReadCsvFile(input_csv, headers, rows) || rows.empty()) {
      continue;
    }
    std::unordered_map<std::string, size_t> index;
    ParseHeadersToIndex(headers, index);
    auto entry_it = index.find(entry_field);
    auto bar_index_it = index.find("bar_index");
    if (entry_it == index.end() || bar_index_it == index.end()) {
      EmitRuntimeError(
          sink,
          "E_NOT_IMPLEMENTED",
          "CHK_RUNTIME_LAYER3_SOURCE_COLUMNS_NOT_IMPLEMENTED",
          "Layer3 bucket_eval requires entry_price_field and bar_index in event CSV.",
          "entry_price_field + bar_index present",
          entry_it == index.end() ? entry_field : "bar_index",
          "/outputs/layer3/outcomes");
      return false;
    }

    const int max_bar = std::min(snapshot.base_data[SC_LAST].GetArraySize(), snapshot.date_times.GetArraySize()) - 1;
    for (size_t r = 0; r < rows.size(); ++r) {
      const auto& row = rows[r];
      if (entry_it->second >= row.size() || bar_index_it->second >= row.size()) {
        continue;
      }
      int bar_index = -1;
      if (!ParseInt(row[bar_index_it->second], bar_index)) {
        continue;
      }
      bar_index = std::max(0, std::min(max_bar, bar_index));
      double entry = 0.0;
      if (!ParseDouble(row[entry_it->second], entry) || std::fabs(entry) < 1e-12) {
        continue;
      }

      for (int horizon : horizons) {
        const int start = bar_index + 1;
        const int end = std::min(max_bar, bar_index + horizon);
        if (start <= bar_index) {
          EmitRuntimeError(
              sink,
              "E_L3_OUTCOME_LEAKAGE_SAME_BAR",
              "CHK_L3_OUTCOME_LEAKAGE",
              "Layer3 outcomes window included same-bar index t.",
              "start index > t",
              std::to_string(start),
              "/outputs/layer3/outcomes/horizon",
              "Ensure outcomes are measured over t+1..t+H.");
          return false;
        }
        if (start > end) {
          continue;
        }

        double max_high = -std::numeric_limits<double>::infinity();
        double min_low = std::numeric_limits<double>::infinity();
        for (int i = start; i <= end; ++i) {
          max_high = std::max(max_high, SnapshotBarFieldAsNumber(snapshot, i, high_field));
          min_low = std::min(min_low, SnapshotBarFieldAsNumber(snapshot, i, low_field));
        }
        const double close_h = SnapshotBarFieldAsNumber(snapshot, end, close_field);
        const double cost = (cost_type == "fixed_bps") ? (bps / 10000.0) : 0.0;

        for (const auto& side : sides) {
          double mfe_pct = 0.0;
          double mfa_pct = 0.0;
          double ret_pct = 0.0;
          if (side == "short") {
            mfe_pct = (entry - min_low) / entry;
            mfa_pct = (entry - max_high) / entry;
            ret_pct = (entry - close_h) / entry;
          } else {
            mfe_pct = (max_high / entry) - 1.0;
            mfa_pct = (min_low / entry) - 1.0;
            ret_pct = (close_h / entry) - 1.0;
          }
          const double net_ret = ret_pct - cost;

          OutcomeRow out;
          out.symbol = symbol;
          out.event_index = static_cast<int>(r);
          out.event_bar_index = bar_index;
          out.horizon_bars = horizon;
          out.side = side;
          out.entry_price = entry;
          out.mfe_pct = mfe_pct;
          out.mfa_pct = mfa_pct;
          out.ret_pct = ret_pct;
          out.net_ret_pct = net_ret;
          out.numeric_fields["entry_price"] = entry;
          out.numeric_fields["mfe_pct"] = mfe_pct;
          out.numeric_fields["mfa_pct"] = mfa_pct;
          out.numeric_fields["ret_pct"] = ret_pct;
          out.numeric_fields["net_ret_pct"] = net_ret;
          for (size_t i = 0; i < headers.size() && i < row.size(); ++i) {
            double parsed = 0.0;
            if (ParseDouble(row[i], parsed)) {
              out.numeric_fields[headers[i]] = parsed;
            }
          }
          outcomes.push_back(std::move(out));
        }
      }
    }
  }

  if (outcomes.empty()) {
    return true;
  }

  struct BucketDim {
    std::string field;
    std::vector<double> q;
    std::vector<double> cuts;
  };
  std::vector<BucketDim> dims;
  const auto* dims_v = sre::ResolvePointer(plan, "/outputs/layer3/bucketing/dimensions");
  if (dims_v && dims_v->IsArray()) {
    for (const auto& dim : dims_v->AsArray().items) {
      if (!dim.IsObject()) continue;
      if (JsonStringAt(dim, "/method", "quantile") != "quantile") continue;
      const std::string field = JsonStringAt(dim, "/field", "");
      const auto* q_v = ObjectGet(dim, "q");
      if (field.empty() || !q_v || !q_v->IsArray() || q_v->AsArray().items.empty()) continue;
      BucketDim d;
      d.field = field;
      for (const auto& q_item : q_v->AsArray().items) {
        if (q_item.IsNumber()) d.q.push_back(std::max(0.0, std::min(1.0, q_item.AsNumber())));
      }
      if (d.q.empty()) continue;
      std::sort(d.q.begin(), d.q.end());
      d.q.erase(std::unique(d.q.begin(), d.q.end()), d.q.end());
      std::vector<std::pair<double, size_t>> values;
      values.reserve(outcomes.size());
      for (size_t i = 0; i < outcomes.size(); ++i) {
        auto it = outcomes[i].numeric_fields.find(field);
        if (it != outcomes[i].numeric_fields.end()) values.push_back({it->second, i});
      }
      std::stable_sort(values.begin(), values.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
      auto cut_at = [&](double q) {
        if (values.empty()) {
          return 0.0;
        }
        q = std::max(0.0, std::min(1.0, q));
        const double pos = q * static_cast<double>(values.size() - 1);
        const size_t lo = static_cast<size_t>(std::floor(pos));
        const size_t hi = static_cast<size_t>(std::ceil(pos));
        if (lo >= values.size()) {
          return values.back().first;
        }
        if (hi >= values.size() || lo == hi) {
          return values[lo].first;
        }
        const double alpha = pos - static_cast<double>(lo);
        return values[lo].first * (1.0 - alpha) + values[hi].first * alpha;
      };
      for (double q : d.q) d.cuts.push_back(cut_at(q));
      dims.push_back(std::move(d));
    }
  }

  for (auto& out : outcomes) {
    if (dims.empty()) {
      out.bucket_key = "all";
      continue;
    }
    std::ostringstream key;
    for (size_t i = 0; i < dims.size(); ++i) {
      const auto& dim = dims[i];
      auto it = out.numeric_fields.find(dim.field);
      int bucket = 0;
      double lo = std::numeric_limits<double>::quiet_NaN();
      double hi = std::numeric_limits<double>::quiet_NaN();
      if (it != out.numeric_fields.end()) {
        while (bucket < static_cast<int>(dim.cuts.size()) && it->second > dim.cuts[static_cast<size_t>(bucket)]) {
          ++bucket;
        }
        lo = (bucket <= 0) ? -std::numeric_limits<double>::infinity() : dim.cuts[static_cast<size_t>(bucket - 1)];
        hi = (bucket >= static_cast<int>(dim.cuts.size())) ? std::numeric_limits<double>::infinity()
                                                           : dim.cuts[static_cast<size_t>(bucket)];
      } else {
        bucket = -1;
      }
      out.dim_bucket[dim.field] = bucket;
      out.dim_lo[dim.field] = lo;
      out.dim_hi[dim.field] = hi;
      if (i > 0) key << "|";
      key << dim.field << "=b" << bucket;
    }
    out.bucket_key = key.str();
  }

  struct BucketAgg {
    std::string symbol;
    std::string bucket_key;
    int horizon_bars = 0;
    std::string side = "long";
    int n_trades = 0;
    double sum_net = 0.0;
    std::vector<double> mfe;
    std::vector<double> mfa;
    std::unordered_map<std::string, double> dim_min;
    std::unordered_map<std::string, double> dim_max;
    std::unordered_map<std::string, double> dim_lo;
    std::unordered_map<std::string, double> dim_hi;
    bool eligible = true;
  };
  std::map<std::string, BucketAgg> agg;
  auto agg_key = [](const OutcomeRow& out) {
    return out.symbol + "\x1f" + out.bucket_key + "\x1f" + std::to_string(out.horizon_bars) + "\x1f" + out.side;
  };
  for (const auto& out : outcomes) {
    auto& a = agg[agg_key(out)];
    a.symbol = out.symbol;
    a.bucket_key = out.bucket_key;
    a.horizon_bars = out.horizon_bars;
    a.side = out.side;
    a.n_trades += 1;
    a.sum_net += out.net_ret_pct;
    a.mfe.push_back(out.mfe_pct);
    a.mfa.push_back(out.mfa_pct);
    for (const auto& dim : dims) {
      auto dim_value_it = out.numeric_fields.find(dim.field);
      if (dim_value_it != out.numeric_fields.end() && out.dim_bucket.contains(dim.field) && out.dim_bucket.at(dim.field) >= 0) {
        auto min_it = a.dim_min.find(dim.field);
        if (min_it == a.dim_min.end()) {
          a.dim_min[dim.field] = dim_value_it->second;
          a.dim_max[dim.field] = dim_value_it->second;
        } else {
          min_it->second = std::min(min_it->second, dim_value_it->second);
          a.dim_max[dim.field] = std::max(a.dim_max[dim.field], dim_value_it->second);
        }
      }
      if (out.dim_lo.contains(dim.field)) {
        a.dim_lo[dim.field] = out.dim_lo.at(dim.field);
      }
      if (out.dim_hi.contains(dim.field)) {
        a.dim_hi[dim.field] = out.dim_hi.at(dim.field);
      }
    }
  }

  const auto* rules_v = sre::ResolvePointer(plan, "/outputs/layer3/eligibility/rules");
  std::vector<std::vector<std::string>> audit_rows;
  for (auto& [_, a] : agg) {
    std::unordered_map<std::string, std::string> fields;
    fields["n_trades"] = std::to_string(a.n_trades);
    fields["EV"] = FormatNumber(a.n_trades > 0 ? a.sum_net / static_cast<double>(a.n_trades) : 0.0);
    fields["ev_net_ret"] = fields["EV"];
    fields["mfe_p50"] = FormatNumber(QuantileValue(a.mfe, 0.50));
    fields["mfe_p80"] = FormatNumber(QuantileValue(a.mfe, 0.80));
    fields["mfe_p90"] = FormatNumber(QuantileValue(a.mfe, 0.90));
    fields["mfe_p95"] = FormatNumber(QuantileValue(a.mfe, 0.95));
    fields["mfa_p50"] = FormatNumber(QuantileValue(a.mfa, 0.50));
    fields["mfa_p25"] = FormatNumber(QuantileValue(a.mfa, 0.25));
    fields["mfa_p10"] = FormatNumber(QuantileValue(a.mfa, 0.10));
    fields["mfa_p05"] = FormatNumber(QuantileValue(a.mfa, 0.05));
    fields["side"] = a.side;
    fields["horizon_bars"] = std::to_string(a.horizon_bars);
    fields["bucket_key"] = a.bucket_key;

    bool eligible = true;
    if (rules_v && rules_v->IsArray()) {
      for (size_t i = 0; i < rules_v->AsArray().items.size(); ++i) {
        const auto& rule = rules_v->AsArray().items[i];
        std::string reason;
        bool pass = false;
        if (!EvaluateBucketRule(rule, fields, pass, reason)) {
          EmitRuntimeError(
              sink,
              "E_NOT_IMPLEMENTED",
              "CHK_RUNTIME_LAYER3_RULE_EVAL_NOT_IMPLEMENTED",
              "Layer3 eligibility rule could not be evaluated in bucket_eval mode.",
              "valid BucketRule with supported op",
              "unsupported rule",
              "/outputs/layer3/eligibility/rules/" + std::to_string(i));
          return false;
        }
        const std::string rule_id = JsonStringAt(rule, "/id", "rule");
        audit_rows.push_back(
            {a.symbol,
             a.bucket_key,
             a.symbol + "|" + a.bucket_key + "|h=" + std::to_string(a.horizon_bars) + "|side=" + a.side,
             std::to_string(a.horizon_bars),
             a.side,
             rule_id,
             pass ? "pass" : "fail",
             reason});
        if (!pass) {
          eligible = false;
        }
      }
    }
    a.eligible = eligible;
  }

  std::vector<std::string> metric_emit;
  const auto* emit_v = sre::ResolvePointer(plan, "/outputs/layer3/metrics/per_bucket/emit");
  if (emit_v && emit_v->IsArray()) {
    for (const auto& item : emit_v->AsArray().items) {
      if (item.IsString() && !item.AsString().empty()) metric_emit.push_back(item.AsString());
    }
  }
  if (metric_emit.empty()) {
    metric_emit = {"n_trades", "EV", "mfe_p50", "mfe_p80", "mfe_p90", "mfe_p95", "mfa_p50", "mfa_p25", "mfa_p10", "mfa_p05"};
  }

  auto format_bound = [](double value) {
    if (std::isnan(value)) return std::string();
    if (std::isinf(value)) return value < 0 ? std::string("-inf") : std::string("inf");
    return FormatNumber(value);
  };

  auto metric_value = [&](const BucketAgg& a, std::string_view metric) -> std::string {
    if (metric == "n_trades") return std::to_string(a.n_trades);
    if (a.n_trades <= 0) return "";
    if (metric == "EV" || metric == "ev_net_ret") return FormatNumber(a.sum_net / static_cast<double>(a.n_trades));
    if (metric == "mfe_p50") return FormatNumber(QuantileValue(a.mfe, 0.50));
    if (metric == "mfe_p80") return FormatNumber(QuantileValue(a.mfe, 0.80));
    if (metric == "mfe_p90") return FormatNumber(QuantileValue(a.mfe, 0.90));
    if (metric == "mfe_p95") return FormatNumber(QuantileValue(a.mfe, 0.95));
    if (metric == "mfa_p50") return FormatNumber(QuantileValue(a.mfa, 0.50));
    if (metric == "mfa_p25") return FormatNumber(QuantileValue(a.mfa, 0.25));
    if (metric == "mfa_p10") return FormatNumber(QuantileValue(a.mfa, 0.10));
    if (metric == "mfa_p05") return FormatNumber(QuantileValue(a.mfa, 0.05));
    return "";
  };

  std::vector<std::vector<std::string>> outcomes_rows;
  outcomes_rows.reserve(outcomes.size());
  for (const auto& out : outcomes) {
    std::vector<std::string> row = {
        out.symbol, std::to_string(out.event_index), std::to_string(out.event_bar_index), FormatNumber(out.entry_price)};
    if (emit_horizon_column) row.push_back(std::to_string(out.horizon_bars));
    if (emit_side_column) row.push_back(out.side);
    row.push_back(FormatNumber(out.mfe_pct));
    row.push_back(FormatNumber(out.mfa_pct));
    row.push_back(FormatNumber(out.ret_pct));
    row.push_back(FormatNumber(out.net_ret_pct));
    row.push_back(out.bucket_key);
    outcomes_rows.push_back(std::move(row));
  }

  std::vector<std::vector<std::string>> bucket_rows;
  for (const auto& [_, a] : agg) {
    std::vector<std::string> row = {a.symbol, a.bucket_key};
    if (emit_horizon_column) row.push_back(std::to_string(a.horizon_bars));
    if (emit_side_column) row.push_back(a.side);
    row.push_back(a.eligible ? "1" : "0");
    for (const auto& dim : dims) {
      row.push_back(a.dim_min.contains(dim.field) ? FormatNumber(a.dim_min.at(dim.field)) : "");
      row.push_back(a.dim_max.contains(dim.field) ? FormatNumber(a.dim_max.at(dim.field)) : "");
      row.push_back(a.dim_lo.contains(dim.field) ? format_bound(a.dim_lo.at(dim.field)) : "");
      row.push_back(a.dim_hi.contains(dim.field) ? format_bound(a.dim_hi.at(dim.field)) : "");
    }
    for (const auto& metric : metric_emit) row.push_back(metric_value(a, metric));
    bucket_rows.push_back(std::move(row));
  }

  const std::string outcomes_path = JsonStringAt(plan, "/outputs/layer3/artifacts/outcomes_per_event", "");
  if (!outcomes_path.empty()) {
    const bool per_symbol = outcomes_path.find("{symbol}") != std::string::npos;
    std::vector<std::string> headers = {"symbol", "event_index", "event_bar_index", "entry_price"};
    if (emit_horizon_column) headers.push_back("horizon_bars");
    if (emit_side_column) headers.push_back("side");
    headers.insert(
        headers.end(), {"mfe_pct", "mfa_pct", "ret_pct", "net_ret_pct", "bucket_key"});
    if (per_symbol) {
      for (const auto& symbol : symbols) {
        std::vector<std::vector<std::string>> rows;
        for (const auto& row : outcomes_rows) if (!row.empty() && row[0] == symbol) rows.push_back(row);
        WriteCsvFile(ResolveOutputPath(outcomes_path, repo_root, symbol), headers, rows);
      }
    } else {
      WriteCsvFile(ResolveOutputPath(outcomes_path, repo_root, ""), headers, outcomes_rows);
    }
  }

  const std::string bucket_path = JsonStringAt(plan, "/outputs/layer3/artifacts/bucket_stats", "");
  if (!bucket_path.empty()) {
    std::vector<std::string> headers = {"symbol", "bucket_key"};
    if (emit_horizon_column) headers.push_back("horizon_bars");
    if (emit_side_column) headers.push_back("side");
    headers.push_back("eligible");
    for (const auto& dim : dims) {
      headers.push_back(dim.field + "_min");
      headers.push_back(dim.field + "_max");
      headers.push_back(dim.field + "_lo");
      headers.push_back(dim.field + "_hi");
    }
    headers.insert(headers.end(), metric_emit.begin(), metric_emit.end());
    const bool per_symbol = bucket_path.find("{symbol}") != std::string::npos;
    if (per_symbol) {
      for (const auto& symbol : symbols) {
        std::vector<std::vector<std::string>> rows;
        for (const auto& row : bucket_rows) if (!row.empty() && row[0] == symbol) rows.push_back(row);
        WriteCsvFile(ResolveOutputPath(bucket_path, repo_root, symbol), headers, rows);
      }
    } else {
      WriteCsvFile(ResolveOutputPath(bucket_path, repo_root, ""), headers, bucket_rows);
    }
  }

  const std::string audit_path = JsonStringAt(plan, "/outputs/layer3/artifacts/decision_audit", "");
  if (!audit_path.empty()) {
    const std::vector<std::string> headers = {
        "symbol", "bucket_key", "context_id", "horizon_bars", "side", "rule_id", "result", "reason"};
    const bool per_symbol = audit_path.find("{symbol}") != std::string::npos;
    if (per_symbol) {
      for (const auto& symbol : symbols) {
        std::vector<std::vector<std::string>> rows;
        for (const auto& row : audit_rows) if (!row.empty() && row[0] == symbol) rows.push_back(row);
        WriteCsvFile(ResolveOutputPath(audit_path, repo_root, symbol), headers, rows);
      }
    } else {
      WriteCsvFile(ResolveOutputPath(audit_path, repo_root, ""), headers, audit_rows);
    }
  }

  return true;
}

bool EmitLayer3Artifacts(
    SCStudyInterfaceRef sc,
    const sre::JsonValue& plan,
    const std::filesystem::path& repo_root,
    const std::vector<std::string>& symbols,
    sre::layers::DiagnosticSink& sink) {
  const auto* layer3 = sre::ResolvePointer(plan, "/outputs/layer3");
  if (!layer3 || !layer3->IsObject()) {
    return true;
  }
  const auto* enabled = ObjectGet(*layer3, "enabled");
  const bool layer3_enabled = (enabled == nullptr) || !enabled->IsBool() || enabled->AsBool();
  if (!layer3_enabled) {
    return true;
  }

  const std::string mode = JsonStringAt(plan, "/outputs/layer3/mode", "rr_menu");
  if (mode == "bucket_eval") {
    if (Layer3BucketEvalRequested(plan)) {
      return EmitLayer3BucketEvalArtifacts(sc, plan, repo_root, symbols, sink);
    }
    return true;
  }
  return EmitLayer3RrMenuArtifacts(plan, repo_root, symbols, sink);
}

sre::JsonValue BuildExecutionDag(const sre::JsonValue& plan) {
  sre::JsonValue::Array nodes;
  const auto* chains = sre::ResolvePointer(plan, "/chains");
  if (chains && chains->IsObject()) {
    for (const auto& [chain_id, chain_spec] : chains->AsObject().fields) {
      if (!chain_spec.IsObject()) {
        continue;
      }
      const auto* steps = ObjectGet(chain_spec, "steps");
      if (!steps || !steps->IsArray()) {
        continue;
      }
      for (size_t i = 0; i < steps->AsArray().items.size(); ++i) {
        const auto& step = steps->AsArray().items[i];
        if (!step.IsObject()) {
          continue;
        }
        sre::JsonValue::Object node;
        node.fields["id"] = chain_id + "#" + std::to_string(i);
        node.fields["chain_id"] = chain_id;
        node.fields["step_index"] = static_cast<double>(i);
        const auto* kind = ObjectGet(step, "kind");
        node.fields["kind"] = kind && kind->IsString() ? kind->AsString() : "unknown";

        sre::JsonValue::Array depends_on;
        if (i > 0) {
          depends_on.items.emplace_back(chain_id + "#" + std::to_string(i - 1));
        }
        node.fields["depends_on"] = sre::JsonValue(std::move(depends_on));
        nodes.items.emplace_back(std::move(node));
      }
    }
  }

  sre::JsonValue::Object dag;
  dag.fields["kind"] = "execution_dag";
  dag.fields["nodes"] = sre::JsonValue(std::move(nodes));
  return sre::JsonValue(std::move(dag));
}

sre::JsonValue BuildLineageDag(const sre::JsonValue& plan) {
  sre::JsonValue::Array nodes;
  const auto* fields = sre::ResolvePointer(plan, "/outputs/dataset/fields");
  if (fields && fields->IsArray()) {
    for (size_t i = 0; i < fields->AsArray().items.size(); ++i) {
      const auto& f = fields->AsArray().items[i];
      if (!f.IsObject()) {
        continue;
      }
      sre::JsonValue::Object node;
      const auto* id = ObjectGet(f, "id");
      const auto* from = ObjectGet(f, "from");
      node.fields["id"] = id && id->IsString() ? id->AsString() : ("field_" + std::to_string(i));
      node.fields["from"] = from && from->IsString() ? from->AsString() : "";
      node.fields["field_index"] = static_cast<double>(i);
      nodes.items.emplace_back(std::move(node));
    }
  }

  sre::JsonValue::Object dag;
  dag.fields["kind"] = "lineage_dag";
  dag.fields["nodes"] = sre::JsonValue(std::move(nodes));
  return sre::JsonValue(std::move(dag));
}

size_t ErrorCount(const sre::layers::DiagnosticSink& sink) {
  size_t count = 0;
  for (const auto& d : sink.Items()) {
    if (d.severity == "error") {
      ++count;
    }
  }
  return count;
}

struct RunResult {
  int rc = 4;
  int diagnostics_count = 0;
  std::string message;
};

RunResult RunAcsilPlan(
    SCStudyInterfaceRef sc,
    const std::filesystem::path& repo_root,
    const std::filesystem::path& plan_path,
    const std::filesystem::path& diagnostics_jsonl,
    const std::filesystem::path& diagnostics_summary) {
  RunResult out;
  try {
    if (!std::filesystem::exists(plan_path)) {
      out.rc = 3;
      out.message = "Plan file does not exist: " + plan_path.string();
      return out;
    }
    if (!std::filesystem::exists(repo_root / "contracts")) {
      out.rc = 3;
      out.message = "Repo root missing contracts directory: " + (repo_root / "contracts").string();
      return out;
    }

    const sre::JsonValue plan = sre::ParseJsonFile(plan_path);
    sre::Engine engine(repo_root);
    const auto result = engine.ValidatePlan(plan, {});

    sre::layers::DiagnosticSink sink;
    for (const auto& d : result.diagnostics) {
      sink.Emit(d);
    }

    bool final_ok = result.status.ok;
    if (!ValidateDatasetRefScopeAndEmitterCoverage(plan, sink)) {
      final_ok = false;
    }

    const bool artifacts_enabled = JsonBoolAt(plan, "/outputs/artifacts/enabled", true);
    const bool allow_fs_write = JsonBoolAt(plan, "/execution/permissions/allow_filesystem_write", false);
    const bool artifact_emission_requested = artifacts_enabled && allow_fs_write;

    std::filesystem::path base_dir = JsonStringAt(plan, "/outputs/artifacts/base_dir", "artifacts");
    if (base_dir.is_relative()) {
      base_dir = repo_root / base_dir;
    }

    const std::vector<std::string> symbols = PlanSymbols(plan, sc);
    if (final_ok && artifact_emission_requested) {
      std::vector<std::string> emitted_symbols;
      if (!EmitDatasetArtifacts(sc, plan, repo_root, symbols, emitted_symbols, sink)) {
        final_ok = false;
      }
      if (!EmitLayer3Artifacts(sc, plan, repo_root, emitted_symbols, sink)) {
        final_ok = false;
      }
    }

    const bool write_manifest = JsonBoolAt(plan, "/outputs/artifacts/write_run_manifest", true);
    const bool write_metrics = JsonBoolAt(plan, "/outputs/artifacts/write_metrics_summary", true);
    if (artifact_emission_requested) {
      std::filesystem::create_directories(base_dir);

      const sre::JsonValue execution_dag = BuildExecutionDag(plan);
      const sre::JsonValue lineage_dag = BuildLineageDag(plan);
      const std::string plan_hash = sre::HashCanonicalPlan(plan);
      const std::string execution_hash = sre::HashCanonicalPlan(execution_dag);
      const std::string lineage_hash = sre::HashCanonicalPlan(lineage_dag);

      WriteJsonFile(base_dir / "execution_dag.json", execution_dag);
      WriteJsonFile(base_dir / "lineage_dag.json", lineage_dag);

      if (write_manifest) {
        sre::JsonValue::Object manifest;
        manifest.fields["plan_cnf_hash"] = plan_hash;
        manifest.fields["execution_dag_hash"] = execution_hash;
        manifest.fields["lineage_dag_hash"] = lineage_hash;
        manifest.fields["diagnostic_count"] = static_cast<double>(sink.Items().size());
        manifest.fields["status"] = final_ok ? "ok" : "error";
        WriteJsonFile(base_dir / "run_manifest.json", sre::JsonValue(std::move(manifest)));
      }
      if (write_metrics) {
        sre::JsonValue::Object metrics;
        metrics.fields["diagnostic_count"] = static_cast<double>(sink.Items().size());
        metrics.fields["error_count"] = static_cast<double>(ErrorCount(sink));
        metrics.fields["status"] = final_ok ? "ok" : "error";
        WriteJsonFile(base_dir / "metrics_summary.json", sre::JsonValue(std::move(metrics)));
      }
    }

    sink.WriteJsonl(diagnostics_jsonl);
    sink.WriteHumanSummary(diagnostics_summary);

    out.rc = final_ok && !sink.HasErrors() ? 0 : 2;
    out.diagnostics_count = static_cast<int>(sink.Items().size());
    if (out.rc == 0) {
      out.message = "SRE validation and artifact execution passed.";
    } else {
      out.message = "SRE runtime emitted diagnostics. See " + diagnostics_jsonl.string();
    }
    return out;
  } catch (const std::exception& ex) {
    out.rc = 4;
    out.message = std::string("Unhandled exception in ACSIL runtime: ") + ex.what();
    return out;
  } catch (...) {
    out.rc = 4;
    out.message = "Unhandled non-std exception in ACSIL runtime.";
    return out;
  }
}

}  // namespace acsil_runtime

SCSFExport scsf_SREPlanLintBridge(SCStudyInterfaceRef sc) {
  if (sc.SetDefaults) {
    sc.GraphName = "SRE Plan Lint Bridge";
    sc.StudyDescription =
        "Validates SRE JSON plans against layer contracts and executes runtime output emission.";
    sc.AutoLoop = 0;
    sc.FreeDLL = 0;
    sc.GraphRegion = 0;

    sc.Input[0].Name = "Plan JSON File";
    sc.Input[0].SetPathAndFileName("E:\\sre_spec_bundle\\artifacts\\example_plan.v2_6_2.json");
    sc.Input[1].Name = "Diagnostics JSONL";
    sc.Input[1].SetPathAndFileName("E:\\sre_spec_bundle\\artifacts\\diagnostics.ast_checks.jsonl");
    sc.Input[2].Name = "Diagnostics Summary";
    sc.Input[2].SetPathAndFileName("E:\\sre_spec_bundle\\artifacts\\diagnostics.ast_checks.txt");
    sc.Input[3].Name = "Repo Root";
    sc.Input[3].SetPathAndFileName("E:\\sre_spec_bundle");

    sc.Subgraph[0].Name = "ReturnCode";
    sc.Subgraph[0].DrawStyle = DRAWSTYLE_IGNORE;
    sc.Subgraph[1].Name = "HasErrors";
    sc.Subgraph[1].DrawStyle = DRAWSTYLE_IGNORE;
    sc.Subgraph[2].Name = "DiagnosticsCount";
    sc.Subgraph[2].DrawStyle = DRAWSTYLE_IGNORE;
    return;
  }

  std::filesystem::path repo_root = std::filesystem::current_path();
  const std::string repo_root_input = acsil_runtime::ToStdString(sc.Input[3].GetString());
  if (!repo_root_input.empty()) {
    repo_root = std::filesystem::path(repo_root_input);
  }

  auto resolve_path = [&](const SCString& raw) {
    std::filesystem::path p = acsil_runtime::ToStdString(raw);
    if (p.is_relative()) {
      p = repo_root / p;
    }
    return p;
  };

  const std::filesystem::path plan_path = resolve_path(sc.Input[0].GetString());
  const std::filesystem::path diagnostics_jsonl = resolve_path(sc.Input[1].GetString());
  const std::filesystem::path diagnostics_summary = resolve_path(sc.Input[2].GetString());
  const acsil_runtime::RunResult run =
      acsil_runtime::RunAcsilPlan(sc, repo_root, plan_path, diagnostics_jsonl, diagnostics_summary);
  g_sre_last_error_message = run.message;

  const int output_index = std::max(0, sc.ArraySize - 1);
  sc.Subgraph[0][output_index] = static_cast<float>(run.rc);
  sc.Subgraph[1][output_index] = run.rc == 0 ? 0.0f : 1.0f;
  sc.Subgraph[2][output_index] = static_cast<float>(run.diagnostics_count);

  int& has_prev = sc.GetPersistentInt(1001);
  int& last_rc = sc.GetPersistentInt(1002);
  SCString& last_message = sc.GetPersistentSCString(1003);
  const std::string prev_message = acsil_runtime::ToStdString(last_message);

  if (!has_prev || last_rc != run.rc || prev_message != run.message) {
    SCString msg;
    msg.Format(
        "SRE runtime rc=%d diagnostics=%d repo='%s' plan='%s' message='%s'",
        run.rc,
        run.diagnostics_count,
        repo_root.string().c_str(),
        plan_path.string().c_str(),
        run.message.c_str());
    sc.AddMessageToLog(msg, run.rc == 0 ? 0 : 1);
    has_prev = 1;
    last_rc = run.rc;
    last_message = run.message.c_str();
  }
}
#endif

