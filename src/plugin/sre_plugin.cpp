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
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
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
    const std::string& symbol,
    SymbolSnapshot& snapshot,
    sre::layers::DiagnosticSink& sink) {
  snapshot.symbol = symbol;
  snapshot.chart_number = FindChartBySymbol(sc, symbol);
  if (snapshot.chart_number <= 0) {
    EmitRuntimeError(
        sink,
        "E_RUN_DATA_UNAVAILABLE",
        "CHK_RUNTIME_SYMBOL_CHART_NOT_FOUND",
        "Could not resolve a chart for requested universe symbol.",
        "chart exists in chartbook for symbol",
        symbol,
        "/universe/symbols",
        "Open a chart for the symbol or adjust universe.symbols.");
    return false;
  }

  sc.GetChartBaseData(snapshot.chart_number, snapshot.base_data);
  sc.GetChartDateTimeArray(snapshot.chart_number, snapshot.date_times);

  const int close_size = snapshot.base_data[SC_LAST].GetArraySize();
  const int dt_size = snapshot.date_times.GetArraySize();
  snapshot.bar_index = std::min(close_size, dt_size) - 1;
  if (snapshot.bar_index < 0) {
    EmitRuntimeError(
        sink,
        "E_RUN_DATA_UNAVAILABLE",
        "CHK_RUNTIME_BAR_DATA_UNAVAILABLE",
        "Chart does not have bar/time arrays available for dataset emission.",
        "at least one bar in chart arrays",
        symbol,
        "/outputs/dataset/fields",
        "Load historical data for the chart before running the study.");
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
    out_value = ToStdString(sc.DateTimeToString(snapshot.date_times[snapshot.bar_index], FLAG_DT_COMPLETE_DATETIME));
    return true;
  }

  auto bar_value = [&](int field_id) -> std::string {
    const auto& arr = snapshot.base_data[field_id];
    if (snapshot.bar_index >= 0 && snapshot.bar_index < arr.GetArraySize()) {
      return FormatNumber(arr[snapshot.bar_index]);
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

    int idx = snapshot.bar_index;
    if (chart_number != snapshot.chart_number) {
      idx = sc.GetNearestMatchForSCDateTime(chart_number, snapshot.date_times[snapshot.bar_index]);
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

  const bool per_symbol = raw_path.find("{symbol}") != std::string::npos;
  bool ok = true;
  std::vector<std::vector<std::string>> merged_rows;

  for (const auto& symbol : symbols) {
    SymbolSnapshot snapshot;
    if (!LoadSymbolSnapshot(sc, symbol, snapshot, sink)) {
      ok = false;
      continue;
    }

    std::vector<std::string> row;
    row.reserve(fields.size());
    for (const auto& field : fields) {
      std::string value;
      if (!ResolveDatasetFieldValue(sc, plan, field, snapshot, value, sink)) {
        ok = false;
        break;
      }
      row.push_back(std::move(value));
    }
    if (!ok) {
      continue;
    }

    if (per_symbol) {
      const auto out_path = ResolveOutputPath(raw_path, repo_root, symbol);
      const std::vector<std::vector<std::string>> one_row{row};
      if (dataset_format == "csv") {
        WriteCsvFile(out_path, headers, one_row);
      } else {
        WriteJsonlFile(out_path, headers, one_row);
      }
    } else {
      merged_rows.push_back(std::move(row));
    }
  }

  if (!per_symbol && ok) {
    const auto out_path = ResolveOutputPath(raw_path, repo_root, "");
    if (dataset_format == "csv") {
      WriteCsvFile(out_path, headers, merged_rows);
    } else {
      WriteJsonlFile(out_path, headers, merged_rows);
    }
  }

  return ok;
}

bool EmitLayer3Artifacts(
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
    const bool artifacts_enabled = JsonBoolAt(plan, "/outputs/artifacts/enabled", true);
    const bool allow_fs_write = JsonBoolAt(plan, "/execution/permissions/allow_filesystem_write", false);
    const bool artifact_emission_requested = artifacts_enabled && allow_fs_write;

    std::filesystem::path base_dir = JsonStringAt(plan, "/outputs/artifacts/base_dir", "artifacts");
    if (base_dir.is_relative()) {
      base_dir = repo_root / base_dir;
    }

    const std::vector<std::string> symbols = PlanSymbols(plan, sc);
    if (final_ok && artifact_emission_requested) {
      if (!EmitDatasetArtifacts(sc, plan, repo_root, symbols, sink)) {
        final_ok = false;
      }
      if (!EmitLayer3Artifacts(plan, repo_root, symbols, sink)) {
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
    sc.Input[0].SetPathAndFileName("plans/example_plan.v2_6_2.json");
    sc.Input[1].Name = "Diagnostics JSONL";
    sc.Input[1].SetPathAndFileName("artifacts/diagnostics.ast_checks.jsonl");
    sc.Input[2].Name = "Diagnostics Summary";
    sc.Input[2].SetPathAndFileName("artifacts/diagnostics.ast_checks.txt");
    sc.Input[3].Name = "Repo Root";
    sc.Input[3].SetPathAndFileName("");

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

