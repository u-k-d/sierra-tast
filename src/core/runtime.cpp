#include "sre/runtime.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <deque>
#include <fstream>
#include <iomanip>
#include <limits>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace {

using sre::JsonValue;

const JsonValue* ObjectGet(const JsonValue& object_value, std::string_view key) {
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

JsonValue* ObjectGet(JsonValue& object_value, std::string_view key) {
  if (!object_value.IsObject()) {
    return nullptr;
  }
  auto& fields = object_value.AsObject().fields;
  auto it = fields.find(std::string(key));
  if (it == fields.end()) {
    return nullptr;
  }
  return &it->second;
}

std::string EscapeJson(std::string_view value) {
  std::ostringstream oss;
  for (char ch : value) {
    switch (ch) {
      case '\"':
        oss << "\\\"";
        break;
      case '\\':
        oss << "\\\\";
        break;
      case '\n':
        oss << "\\n";
        break;
      case '\r':
        oss << "\\r";
        break;
      case '\t':
        oss << "\\t";
        break;
      default:
        if (static_cast<unsigned char>(ch) < 0x20) {
          oss << "\\u" << std::hex << std::setw(4) << std::setfill('0')
              << static_cast<int>(static_cast<unsigned char>(ch)) << std::dec;
        } else {
          oss << ch;
        }
        break;
    }
  }
  return oss.str();
}

class JsonParser {
public:
  explicit JsonParser(std::string_view input) : input_(input), index_(0) {}

  JsonValue Parse() {
    SkipWhitespace();
    JsonValue value = ParseValue();
    SkipWhitespace();
    if (index_ != input_.size()) {
      throw std::runtime_error("Trailing characters in JSON input");
    }
    return value;
  }

private:
  JsonValue ParseValue() {
    if (index_ >= input_.size()) {
      throw std::runtime_error("Unexpected end of input");
    }
    char ch = input_[index_];
    if (ch == '{') {
      return ParseObject();
    }
    if (ch == '[') {
      return ParseArray();
    }
    if (ch == '"') {
      return JsonValue(ParseString());
    }
    if (ch == 't' || ch == 'f') {
      return JsonValue(ParseBool());
    }
    if (ch == 'n') {
      ParseNull();
      return JsonValue(nullptr);
    }
    if (ch == '-' || std::isdigit(static_cast<unsigned char>(ch))) {
      return JsonValue(ParseNumber());
    }
    throw std::runtime_error("Invalid JSON token");
  }

  JsonValue ParseObject() {
    Expect('{');
    JsonValue::Object object;
    SkipWhitespace();
    if (TryConsume('}')) {
      return JsonValue(object);
    }
    while (true) {
      SkipWhitespace();
      std::string key = ParseString();
      SkipWhitespace();
      Expect(':');
      SkipWhitespace();
      object.fields.emplace(std::move(key), ParseValue());
      SkipWhitespace();
      if (TryConsume('}')) {
        break;
      }
      Expect(',');
      SkipWhitespace();
    }
    return JsonValue(std::move(object));
  }

  JsonValue ParseArray() {
    Expect('[');
    JsonValue::Array array;
    SkipWhitespace();
    if (TryConsume(']')) {
      return JsonValue(array);
    }
    while (true) {
      SkipWhitespace();
      array.items.push_back(ParseValue());
      SkipWhitespace();
      if (TryConsume(']')) {
        break;
      }
      Expect(',');
      SkipWhitespace();
    }
    return JsonValue(std::move(array));
  }

  std::string ParseString() {
    Expect('"');
    std::string out;
    while (index_ < input_.size()) {
      char ch = input_[index_++];
      if (ch == '"') {
        return out;
      }
      if (ch == '\\') {
        if (index_ >= input_.size()) {
          throw std::runtime_error("Invalid escape sequence");
        }
        char esc = input_[index_++];
        switch (esc) {
          case '"':
            out.push_back('"');
            break;
          case '\\':
            out.push_back('\\');
            break;
          case '/':
            out.push_back('/');
            break;
          case 'b':
            out.push_back('\b');
            break;
          case 'f':
            out.push_back('\f');
            break;
          case 'n':
            out.push_back('\n');
            break;
          case 'r':
            out.push_back('\r');
            break;
          case 't':
            out.push_back('\t');
            break;
          case 'u':
            // Minimal unicode handling: keep parser permissive and substitute marker.
            if (index_ + 4 > input_.size()) {
              throw std::runtime_error("Invalid unicode escape");
            }
            index_ += 4;
            out.push_back('?');
            break;
          default:
            throw std::runtime_error("Invalid escape character");
        }
      } else {
        out.push_back(ch);
      }
    }
    throw std::runtime_error("Unterminated string");
  }

  bool ParseBool() {
    if (input_.substr(index_, 4) == "true") {
      index_ += 4;
      return true;
    }
    if (input_.substr(index_, 5) == "false") {
      index_ += 5;
      return false;
    }
    throw std::runtime_error("Invalid boolean token");
  }

  void ParseNull() {
    if (input_.substr(index_, 4) != "null") {
      throw std::runtime_error("Invalid null token");
    }
    index_ += 4;
  }

  double ParseNumber() {
    size_t start = index_;
    if (input_[index_] == '-') {
      ++index_;
    }
    while (index_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[index_]))) {
      ++index_;
    }
    if (index_ < input_.size() && input_[index_] == '.') {
      ++index_;
      while (index_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[index_]))) {
        ++index_;
      }
    }
    if (index_ < input_.size() && (input_[index_] == 'e' || input_[index_] == 'E')) {
      ++index_;
      if (index_ < input_.size() && (input_[index_] == '+' || input_[index_] == '-')) {
        ++index_;
      }
      while (index_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[index_]))) {
        ++index_;
      }
    }
    return std::stod(std::string(input_.substr(start, index_ - start)));
  }

  void SkipWhitespace() {
    while (index_ < input_.size() && std::isspace(static_cast<unsigned char>(input_[index_]))) {
      ++index_;
    }
  }

  void Expect(char ch) {
    if (index_ >= input_.size() || input_[index_] != ch) {
      throw std::runtime_error("Unexpected token while parsing JSON");
    }
    ++index_;
  }

  bool TryConsume(char ch) {
    if (index_ < input_.size() && input_[index_] == ch) {
      ++index_;
      return true;
    }
    return false;
  }

  std::string_view input_;
  size_t index_;
};

void DumpCanonical(const JsonValue& value, std::ostringstream& oss) {
  if (value.IsNull()) {
    oss << "null";
    return;
  }
  if (value.IsBool()) {
    oss << (value.AsBool() ? "true" : "false");
    return;
  }
  if (value.IsNumber()) {
    oss << std::setprecision(17) << value.AsNumber();
    return;
  }
  if (value.IsString()) {
    oss << "\"" << EscapeJson(value.AsString()) << "\"";
    return;
  }
  if (value.IsArray()) {
    oss << "[";
    bool first = true;
    for (const auto& item : value.AsArray().items) {
      if (!first) {
        oss << ",";
      }
      first = false;
      DumpCanonical(item, oss);
    }
    oss << "]";
    return;
  }
  oss << "{";
  bool first = true;
  for (const auto& [key, item] : value.AsObject().fields) {
    if (!first) {
      oss << ",";
    }
    first = false;
    oss << "\"" << EscapeJson(key) << "\":";
    DumpCanonical(item, oss);
  }
  oss << "}";
}

std::string PointerDecode(std::string token) {
  size_t pos = 0;
  while ((pos = token.find("~1", pos)) != std::string::npos) {
    token.replace(pos, 2, "/");
  }
  pos = 0;
  while ((pos = token.find("~0", pos)) != std::string::npos) {
    token.replace(pos, 2, "~");
  }
  return token;
}

std::vector<std::string> SplitPointer(std::string_view pointer) {
  if (pointer.empty()) {
    return {};
  }
  if (pointer[0] != '/') {
    throw std::runtime_error("JSON pointer must start with '/'");
  }
  std::vector<std::string> tokens;
  size_t start = 1;
  while (start <= pointer.size()) {
    size_t slash = pointer.find('/', start);
    std::string part = std::string(pointer.substr(start, slash == std::string_view::npos ? pointer.size() - start : slash - start));
    tokens.push_back(PointerDecode(part));
    if (slash == std::string_view::npos) {
      break;
    }
    start = slash + 1;
  }
  return tokens;
}

inline uint32_t Rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }

std::string Sha256Hex(const std::string& data) {
  static const std::array<uint32_t, 64> k = {
      0x428a2f98,
      0x71374491,
      0xb5c0fbcf,
      0xe9b5dba5,
      0x3956c25b,
      0x59f111f1,
      0x923f82a4,
      0xab1c5ed5,
      0xd807aa98,
      0x12835b01,
      0x243185be,
      0x550c7dc3,
      0x72be5d74,
      0x80deb1fe,
      0x9bdc06a7,
      0xc19bf174,
      0xe49b69c1,
      0xefbe4786,
      0x0fc19dc6,
      0x240ca1cc,
      0x2de92c6f,
      0x4a7484aa,
      0x5cb0a9dc,
      0x76f988da,
      0x983e5152,
      0xa831c66d,
      0xb00327c8,
      0xbf597fc7,
      0xc6e00bf3,
      0xd5a79147,
      0x06ca6351,
      0x14292967,
      0x27b70a85,
      0x2e1b2138,
      0x4d2c6dfc,
      0x53380d13,
      0x650a7354,
      0x766a0abb,
      0x81c2c92e,
      0x92722c85,
      0xa2bfe8a1,
      0xa81a664b,
      0xc24b8b70,
      0xc76c51a3,
      0xd192e819,
      0xd6990624,
      0xf40e3585,
      0x106aa070,
      0x19a4c116,
      0x1e376c08,
      0x2748774c,
      0x34b0bcb5,
      0x391c0cb3,
      0x4ed8aa4a,
      0x5b9cca4f,
      0x682e6ff3,
      0x748f82ee,
      0x78a5636f,
      0x84c87814,
      0x8cc70208,
      0x90befffa,
      0xa4506ceb,
      0xbef9a3f7,
      0xc67178f2,
  };

  uint64_t bitlen = static_cast<uint64_t>(data.size()) * 8ULL;
  std::vector<uint8_t> buf(data.begin(), data.end());
  buf.push_back(0x80);
  while ((buf.size() % 64) != 56) {
    buf.push_back(0x00);
  }
  for (int i = 7; i >= 0; --i) {
    buf.push_back(static_cast<uint8_t>((bitlen >> (i * 8)) & 0xFF));
  }

  uint32_t h0 = 0x6a09e667;
  uint32_t h1 = 0xbb67ae85;
  uint32_t h2 = 0x3c6ef372;
  uint32_t h3 = 0xa54ff53a;
  uint32_t h4 = 0x510e527f;
  uint32_t h5 = 0x9b05688c;
  uint32_t h6 = 0x1f83d9ab;
  uint32_t h7 = 0x5be0cd19;

  for (size_t chunk = 0; chunk < buf.size(); chunk += 64) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
      w[i] = (uint32_t(buf[chunk + i * 4]) << 24) | (uint32_t(buf[chunk + i * 4 + 1]) << 16) |
             (uint32_t(buf[chunk + i * 4 + 2]) << 8) | uint32_t(buf[chunk + i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i) {
      uint32_t s0 = Rotr(w[i - 15], 7) ^ Rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
      uint32_t s1 = Rotr(w[i - 2], 17) ^ Rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = h0;
    uint32_t b = h1;
    uint32_t c = h2;
    uint32_t d = h3;
    uint32_t e = h4;
    uint32_t f = h5;
    uint32_t g = h6;
    uint32_t h = h7;

    for (int i = 0; i < 64; ++i) {
      uint32_t s1 = Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25);
      uint32_t ch = (e & f) ^ ((~e) & g);
      uint32_t temp1 = h + s1 + ch + k[static_cast<size_t>(i)] + w[i];
      uint32_t s0 = Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22);
      uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
      uint32_t temp2 = s0 + maj;
      h = g;
      g = f;
      f = e;
      e = d + temp1;
      d = c;
      c = b;
      b = a;
      a = temp1 + temp2;
    }

    h0 += a;
    h1 += b;
    h2 += c;
    h3 += d;
    h4 += e;
    h5 += f;
    h6 += g;
    h7 += h;
  }

  std::array<uint8_t, 32> digest{};
  auto put_word = [&](int offset, uint32_t v) {
    digest[static_cast<size_t>(offset + 0)] = static_cast<uint8_t>((v >> 24) & 0xFF);
    digest[static_cast<size_t>(offset + 1)] = static_cast<uint8_t>((v >> 16) & 0xFF);
    digest[static_cast<size_t>(offset + 2)] = static_cast<uint8_t>((v >> 8) & 0xFF);
    digest[static_cast<size_t>(offset + 3)] = static_cast<uint8_t>(v & 0xFF);
  };
  put_word(0, h0);
  put_word(4, h1);
  put_word(8, h2);
  put_word(12, h3);
  put_word(16, h4);
  put_word(20, h5);
  put_word(24, h6);
  put_word(28, h7);

  static const char* hex = "0123456789abcdef";
  std::string out;
  out.reserve(64);
  for (uint8_t b : digest) {
    out.push_back(hex[b >> 4]);
    out.push_back(hex[b & 0x0F]);
  }
  return out;
}

bool JsonBoolAt(const JsonValue& root, std::string_view pointer, bool default_value) {
  const auto* v = sre::ResolvePointer(root, pointer);
  if (!v || !v->IsBool()) {
    return default_value;
  }
  return v->AsBool();
}

std::string JsonStringAt(const JsonValue& root, std::string_view pointer, std::string default_value) {
  const auto* v = sre::ResolvePointer(root, pointer);
  if (!v || !v->IsString()) {
    return default_value;
  }
  if (v->AsString().empty()) {
    return default_value;
  }
  return v->AsString();
}

JsonValue::Array JsonArrayFromStrings(const std::vector<std::string>& values) {
  JsonValue::Array arr;
  for (const auto& v : values) {
    arr.items.emplace_back(v);
  }
  return arr;
}

JsonValue BuildExecutionDag(const JsonValue& plan) {
  JsonValue::Array nodes;
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
        JsonValue::Object node;
        node.fields["id"] = chain_id + "#" + std::to_string(i);
        node.fields["chain_id"] = chain_id;
        node.fields["step_index"] = static_cast<double>(i);
        const auto* kind = ObjectGet(step, "kind");
        node.fields["kind"] = kind && kind->IsString() ? kind->AsString() : "unknown";

        JsonValue::Array depends_on;
        if (i > 0) {
          depends_on.items.emplace_back(chain_id + "#" + std::to_string(i - 1));
        }
        node.fields["depends_on"] = JsonValue(std::move(depends_on));
        nodes.items.emplace_back(std::move(node));
      }
    }
  }

  JsonValue::Object dag;
  dag.fields["kind"] = "execution_dag";
  dag.fields["nodes"] = JsonValue(std::move(nodes));
  return JsonValue(std::move(dag));
}

JsonValue BuildLineageDag(const JsonValue& plan) {
  JsonValue::Array nodes;
  const auto* fields = sre::ResolvePointer(plan, "/outputs/dataset/fields");
  if (fields && fields->IsArray()) {
    for (size_t i = 0; i < fields->AsArray().items.size(); ++i) {
      const auto& f = fields->AsArray().items[i];
      if (!f.IsObject()) {
        continue;
      }
      JsonValue::Object node;
      const auto* id = ObjectGet(f, "id");
      const auto* from = ObjectGet(f, "from");
      node.fields["id"] = id && id->IsString() ? id->AsString() : ("field_" + std::to_string(i));
      node.fields["from"] = from && from->IsString() ? from->AsString() : "";
      node.fields["field_index"] = static_cast<double>(i);
      nodes.items.emplace_back(std::move(node));
    }
  }

  JsonValue::Object dag;
  dag.fields["kind"] = "lineage_dag";
  dag.fields["nodes"] = JsonValue(std::move(nodes));
  return JsonValue(std::move(dag));
}

bool PointerInOwnedScope(const std::string& pointer, const std::vector<std::string>& owned_pointers) {
  for (const auto& owned : owned_pointers) {
    if (pointer == owned) {
      return true;
    }
    if (pointer.size() > owned.size() && pointer.rfind(owned + "/", 0) == 0) {
      return true;
    }
  }
  return false;
}

std::optional<std::string> InvalidFixtureOrdinal(std::string_view fixture_name) {
  static const std::regex re(".*[Ii][Nn][Vv][Aa][Ll][Ii][Dd]_([0-9]{2})$");
  std::match_results<std::string_view::const_iterator> match;
  if (!std::regex_match(fixture_name.begin(), fixture_name.end(), match, re) || match.size() != 2) {
    return std::nullopt;
  }
  return std::string(match[1].first, match[1].second);
}

std::unordered_map<std::string, std::vector<std::string>> LoadAllowedLayerDepsFromLock(const std::filesystem::path& repo_root) {
  std::unordered_map<std::string, std::vector<std::string>> allowed;
  const auto lock_path = repo_root / "layers.lock.json";
  if (!std::filesystem::exists(lock_path)) {
    return allowed;
  }
  const JsonValue lock = sre::ParseJsonFile(lock_path);
  const auto* deps = ObjectGet(lock, "allowed_deps");
  if (!deps || !deps->IsArray()) {
    return allowed;
  }
  for (const auto& dep : deps->AsArray().items) {
    if (!dep.IsObject()) {
      continue;
    }
    const auto* from = ObjectGet(dep, "from");
    const auto* to_layers = ObjectGet(dep, "to_layers");
    if (!from || !from->IsString() || !to_layers || !to_layers->IsArray()) {
      continue;
    }
    std::vector<std::string> out;
    for (const auto& l : to_layers->AsArray().items) {
      if (l.IsString()) {
        out.push_back(l.AsString());
      }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    allowed[from->AsString()] = std::move(out);
  }
  return allowed;
}

void EmitManifestCheckFailure(
    sre::layers::DiagnosticSink& sink,
    const sre::LayerManifest& manifest,
    std::string code,
    std::string check_id,
    std::string group_id,
    std::string reason,
    std::string expected,
    std::string actual,
    std::vector<std::string> blame_pointers,
    std::string remediation,
    std::string severity) {
  sre::layers::Diagnostic d;
  d.code = std::move(code);
  d.layer_id = manifest.layer_id;
  d.check_id = std::move(check_id);
  d.group_id = std::move(group_id);
  d.reason = std::move(reason);
  d.expected = std::move(expected);
  d.actual = std::move(actual);
  d.blame_pointers = std::move(blame_pointers);
  d.remediation = std::move(remediation);
  d.severity = std::move(severity);
  d.source = manifest.path.string();
  sink.Emit(d);
}

bool DiagnosticExists(
    const std::vector<sre::layers::Diagnostic>& diagnostics,
    std::string_view check_id,
    std::string_view code,
    std::string_view blame_pointer) {
  for (const auto& d : diagnostics) {
    if (d.check_id != check_id) {
      continue;
    }
    if (!code.empty() && d.code != code) {
      continue;
    }
    if (!blame_pointer.empty()) {
      if (std::find(d.blame_pointers.begin(), d.blame_pointers.end(), blame_pointer) == d.blame_pointers.end()) {
        continue;
      }
    }
    return true;
  }
  return false;
}

void ExecuteManifestChecks(
    const sre::LayerManifest& manifest,
    const JsonValue& plan,
    const std::filesystem::path& repo_root,
    const std::unordered_map<std::string, std::vector<std::string>>& lock_allowed_layer_deps,
    bool runtime_validator_registered,
    const std::vector<sre::layers::Diagnostic>& diagnostics,
    sre::layers::DiagnosticSink& sink) {
  const auto* normative = ObjectGet(manifest.manifest, "normative_ast_checks");
  const auto* groups = normative && normative->IsObject() ? ObjectGet(*normative, "check_groups") : nullptr;
  if (!groups || !groups->IsArray()) {
    EmitManifestCheckFailure(
        sink,
        manifest,
        manifest.primary_error_code,
        "CHK_MANIFEST_CHECK_GROUPS_PRESENT",
        "CHKGRP_PLAN_AST_SHAPE",
        "Manifest must define normative_ast_checks.check_groups.",
        "array[minItems=1]",
        "missing",
        {},
        "Populate normative_ast_checks.check_groups in the layer manifest.",
        "error");
    return;
  }

  std::optional<std::string> plan_fixture_ord;
  if (const auto* fixture_id = sre::ResolvePointer(plan, "/meta/fixture_id"); fixture_id && fixture_id->IsString()) {
    plan_fixture_ord = InvalidFixtureOrdinal(fixture_id->AsString());
  }
  const bool invalid_fixture = JsonBoolAt(plan, "/compat/invalid_case", false);

  for (const auto& group : groups->AsArray().items) {
    if (!group.IsObject()) {
      continue;
    }
    const auto* group_id = ObjectGet(group, "group_id");
    const auto* checks = ObjectGet(group, "checks");
    if (!group_id || !group_id->IsString() || !checks || !checks->IsArray()) {
      continue;
    }
    const std::string group_id_text = group_id->AsString();
    for (const auto& check : checks->AsArray().items) {
      if (!check.IsObject()) {
        continue;
      }
      const auto* id = ObjectGet(check, "id");
      const auto* severity = ObjectGet(check, "severity");
      const auto* assertion = ObjectGet(check, "assert");
      if (!id || !id->IsString() || !severity || !severity->IsString() || !assertion || !assertion->IsObject()) {
        continue;
      }
      const auto* assert_kind = ObjectGet(*assertion, "kind");
      const auto* params = ObjectGet(*assertion, "params");
      if (!assert_kind || !assert_kind->IsString()) {
        continue;
      }

      const std::string check_id = id->AsString();
      const std::string sev = severity->AsString();
      const std::string kind = assert_kind->AsString();

      auto fail = [&](const std::string& reason, const std::string& expected, const std::string& actual, std::vector<std::string> blame = {}) {
        EmitManifestCheckFailure(
            sink,
            manifest,
            manifest.primary_error_code,
            check_id,
            group_id_text,
            reason,
            expected,
            actual,
            std::move(blame),
            "Update implementation or manifest so normative check passes.",
            sev);
      };

      if (kind == "function_exists_with_signature") {
        if (!runtime_validator_registered) {
          fail("Runtime validator for layer is not registered.", "registered validator", "missing");
          continue;
        }
        if (!params || !params->IsObject()) {
          fail("function_exists_with_signature requires params object.", "params", "missing");
          continue;
        }
        const auto* fn = ObjectGet(*params, "function");
        const auto* ns = ObjectGet(*params, "namespace");
        const auto* ret = ObjectGet(*params, "return_type");
        if (!fn || !fn->IsString() || !ns || !ns->IsString() || !ret || !ret->IsString()) {
          fail("function_exists_with_signature params are incomplete.", "function+namespace+return_type", "missing");
          continue;
        }
        bool found = false;
        const auto* public_api = ObjectGet(manifest.manifest, "public_api");
        const auto* functions = public_api && public_api->IsObject() ? ObjectGet(*public_api, "functions") : nullptr;
        if (functions && functions->IsArray()) {
          for (const auto& f : functions->AsArray().items) {
            if (!f.IsObject()) {
              continue;
            }
            const auto* name = ObjectGet(f, "name");
            const auto* sig = ObjectGet(f, "signature");
            if (!name || !name->IsString() || !sig || !sig->IsObject()) {
              continue;
            }
            const auto* sig_ns = ObjectGet(*sig, "namespace");
            const auto* sig_ret = ObjectGet(*sig, "return_type");
            if (!sig_ns || !sig_ns->IsString() || !sig_ret || !sig_ret->IsString()) {
              continue;
            }
            if (name->AsString() == fn->AsString() && sig_ns->AsString() == ns->AsString() && sig_ret->AsString() == ret->AsString()) {
              found = true;
              break;
            }
          }
        }
        if (!found) {
          fail("Function signature from normative check does not match manifest public_api.", fn->AsString(), "not found");
        }
        continue;
      }

      if (kind == "file_exists") {
        const auto* path = params && params->IsObject() ? ObjectGet(*params, "path") : nullptr;
        if (!path || !path->IsString()) {
          fail("file_exists requires params.path string.", "path string", "missing");
          continue;
        }
        if (!std::filesystem::exists(repo_root / path->AsString())) {
          fail("Required file path is missing.", path->AsString(), "missing", {path->AsString()});
        }
        continue;
      }

      if (kind == "file_forbidden") {
        const auto* path = params && params->IsObject() ? ObjectGet(*params, "path") : nullptr;
        if (!path || !path->IsString()) {
          fail("file_forbidden requires params.path string.", "path string", "missing");
          continue;
        }
        if (std::filesystem::exists(repo_root / path->AsString())) {
          fail("Forbidden file path exists.", "path absent", "present", {path->AsString()});
        }
        continue;
      }

      if (kind == "only_allowed_layer_deps") {
        const auto* allowed = params && params->IsObject() ? ObjectGet(*params, "allowed_layer_ids") : nullptr;
        if (!allowed || !allowed->IsArray()) {
          fail("only_allowed_layer_deps requires params.allowed_layer_ids.", "array", "missing");
          continue;
        }
        const auto lock_it = lock_allowed_layer_deps.find(manifest.layer_id);
        const std::vector<std::string> lock_allowed = lock_it == lock_allowed_layer_deps.end() ? std::vector<std::string>{} : lock_it->second;
        for (const auto& dep : allowed->AsArray().items) {
          if (!dep.IsString()) {
            continue;
          }
          if (std::find(lock_allowed.begin(), lock_allowed.end(), dep.AsString()) == lock_allowed.end()) {
            fail("Declared layer dependency is outside layers.lock policy.", "subset of layers.lock", dep.AsString(), {"/dependencies/allowed_layer_ids"});
            break;
          }
        }
        continue;
      }

      if (kind == "cnf_has_node") {
        const auto* node = params && params->IsObject() ? ObjectGet(*params, "node_id") : nullptr;
        if (!node || !node->IsString()) {
          fail("cnf_has_node requires params.node_id.", "node_id string", "missing");
          continue;
        }
        bool declared = false;
        const auto* ast_nodes = ObjectGet(manifest.manifest, "ast_nodes");
        if (ast_nodes && ast_nodes->IsArray()) {
          for (const auto& n : ast_nodes->AsArray().items) {
            const auto* id_field = n.IsObject() ? ObjectGet(n, "node_id") : nullptr;
            if (id_field && id_field->IsString() && id_field->AsString() == node->AsString()) {
              declared = true;
              break;
            }
          }
        }
        bool has_scope_data = false;
        for (const auto& p : manifest.owned_pointers) {
          if (sre::PointerExists(plan, p)) {
            has_scope_data = true;
            break;
          }
        }
        if (!has_scope_data) {
          continue;
        }
        if (!declared) {
          fail("CNF node requirement not satisfied for this plan/layer.", node->AsString(), "node not declared");
        }
        continue;
      }

      if (kind == "cnf_pointer_in_scope") {
        const auto* pointers = params && params->IsObject() ? ObjectGet(*params, "pointers") : nullptr;
        if (!pointers || !pointers->IsArray()) {
          fail("cnf_pointer_in_scope requires params.pointers.", "array", "missing");
          continue;
        }
        for (const auto& p : pointers->AsArray().items) {
          if (!p.IsString()) {
            continue;
          }
          if (!PointerInOwnedScope(p.AsString(), manifest.owned_pointers)) {
            fail("Pointer listed by check is outside manifest owned scope.", "pointer in schema_scope.owned_pointers", p.AsString(), {p.AsString()});
            break;
          }
        }
        continue;
      }

      if (kind == "invalid_fixture_yields_error") {
        if (!invalid_fixture) {
          continue;
        }
        const auto* fixture_id = params && params->IsObject() ? ObjectGet(*params, "fixture_id") : nullptr;
        const auto* expected_code = params && params->IsObject() ? ObjectGet(*params, "expected_code") : nullptr;
        const auto* expected_blame = params && params->IsObject() ? ObjectGet(*params, "expected_blame_pointer") : nullptr;
        if (!fixture_id || !fixture_id->IsString() || !expected_code || !expected_code->IsString()) {
          fail("invalid_fixture_yields_error params are incomplete.", "fixture_id+expected_code", "missing");
          continue;
        }
        const auto check_ord = InvalidFixtureOrdinal(fixture_id->AsString());
        if (plan_fixture_ord && check_ord && *plan_fixture_ord != *check_ord) {
          continue;
        }
        const std::string blame = expected_blame && expected_blame->IsString() ? expected_blame->AsString() : "";
        if (!DiagnosticExists(diagnostics, check_id, expected_code->AsString(), blame)) {
          fail(
              "Expected invalid-fixture diagnostic not emitted.",
              expected_code->AsString(),
              "diagnostic missing",
              blame.empty() ? std::vector<std::string>{} : std::vector<std::string>{blame});
        }
        continue;
      }

      EmitManifestCheckFailure(
          sink,
          manifest,
          "E_LAYER_MANIFEST_CHECK_UNSUPPORTED",
          check_id,
          group_id_text,
          "Manifest check kind is not implemented by runtime.",
          "implemented check kind",
          kind,
          {},
          "Implement this check kind in runtime manifest executor.",
          "error");
    }
  }
}

std::string ReplaceAll(std::string text, std::string_view needle, std::string_view replacement) {
  size_t pos = 0;
  while ((pos = text.find(needle, pos)) != std::string::npos) {
    text.replace(pos, needle.size(), replacement);
    pos += replacement.size();
  }
  return text;
}

std::filesystem::path ResolvePlanOutputPath(const std::string& raw_path, const std::filesystem::path& repo_root, std::string_view symbol) {
  std::string expanded = raw_path;
  expanded = ReplaceAll(std::move(expanded), "{symbol}", symbol);
  std::filesystem::path out = expanded;
  if (out.is_relative()) {
    out = repo_root / out;
  }
  return out;
}

bool PathTemplateUsesSymbol(const std::string& raw_path) { return raw_path.find("{symbol}") != std::string::npos; }

std::vector<std::string> PlanUniverseSymbols(const JsonValue& plan) {
  std::vector<std::string> symbols;
  const auto* universe_symbols = sre::ResolvePointer(plan, "/universe/symbols");
  if (universe_symbols && universe_symbols->IsArray()) {
    for (const auto& s : universe_symbols->AsArray().items) {
      if (s.IsString() && !s.AsString().empty()) {
        symbols.push_back(s.AsString());
      }
    }
  }
  if (symbols.empty()) {
    symbols.push_back("UNSPECIFIED");
  }
  return symbols;
}

struct DatasetFieldSpec {
  std::string name;
  std::string source_ref;
  std::string source_pointer;
};

std::vector<DatasetFieldSpec> DatasetFieldsFromPlan(const JsonValue& plan) {
  std::vector<DatasetFieldSpec> fields;
  const auto* field_array = sre::ResolvePointer(plan, "/outputs/dataset/fields");
  if (!field_array || !field_array->IsArray()) {
    return fields;
  }
  for (size_t i = 0; i < field_array->AsArray().items.size(); ++i) {
    const auto& field = field_array->AsArray().items[i];
    if (!field.IsObject()) {
      continue;
    }

    const auto* name = ObjectGet(field, "name");
    const auto* id = ObjectGet(field, "id");
    const auto* ref = ObjectGet(field, "ref");
    const auto* from = ObjectGet(field, "from");

    DatasetFieldSpec spec;
    if (name && name->IsString() && !name->AsString().empty()) {
      spec.name = name->AsString();
    } else if (id && id->IsString() && !id->AsString().empty()) {
      spec.name = id->AsString();
    } else {
      spec.name = "field_" + std::to_string(i);
    }

    if (ref && ref->IsString()) {
      spec.source_ref = ref->AsString();
      spec.source_pointer = "/outputs/dataset/fields/" + std::to_string(i) + "/ref";
    } else if (from && from->IsString()) {
      spec.source_ref = from->AsString();
      spec.source_pointer = "/outputs/dataset/fields/" + std::to_string(i) + "/from";
    }

    fields.push_back(std::move(spec));
  }
  return fields;
}

std::string FormatNumber(double value) {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(8) << value;
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

bool ParseDoubleStrict(std::string_view text, double& out) {
  if (text.empty()) {
    return false;
  }
  try {
    size_t pos = 0;
    const std::string s(text);
    out = std::stod(s, &pos);
    return pos == s.size();
  } catch (...) {
    return false;
  }
}

bool ParseIntStrict(std::string_view text, int& out) {
  if (text.empty()) {
    return false;
  }
  try {
    size_t pos = 0;
    const std::string s(text);
    const long v = std::stol(s, &pos, 10);
    if (pos != s.size()) {
      return false;
    }
    if (v < std::numeric_limits<int>::min() || v > std::numeric_limits<int>::max()) {
      return false;
    }
    out = static_cast<int>(v);
    return true;
  } catch (...) {
    return false;
  }
}

struct PlanDateRange {
  bool enabled = false;
  std::string start_date;
  std::string end_date;
};

PlanDateRange ParsePlanDateRange(const JsonValue& plan) {
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

bool DateInRange(std::string_view date_time_text, const PlanDateRange& range) {
  if (!range.enabled) {
    return true;
  }
  if (date_time_text.size() < 10) {
    return false;
  }
  const std::string date(date_time_text.substr(0, 10));
  return date >= range.start_date && date <= range.end_date;
}

std::string JsonTypeName(const JsonValue* value) { return value ? value->TypeName() : "missing"; }

void EmitRuntimeFeatureError(
    sre::layers::DiagnosticSink& sink,
    std::string code,
    std::string check_id,
    std::string reason,
    std::string expected,
    std::string actual,
    std::string pointer,
    std::string remediation) {
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
      "runtime");
}

void EmitRuntimeFeatureWarning(
    sre::layers::DiagnosticSink& sink,
    std::string code,
    std::string check_id,
    std::string reason,
    std::string expected,
    std::string actual,
    std::vector<std::string> blame_pointers,
    std::string remediation) {
  sre::layers::Diagnostic d;
  d.code = std::move(code);
  d.layer_id = "outputs_repro";
  d.check_id = std::move(check_id);
  d.group_id = "CHKGRP_PLAN_AST_SHAPE";
  d.reason = std::move(reason);
  d.expected = std::move(expected);
  d.actual = std::move(actual);
  d.blame_pointers = std::move(blame_pointers);
  d.remediation = std::move(remediation);
  d.severity = "warning";
  d.source = "runtime";
  sink.Emit(d);
}

bool IsImplementedDatasetRuntimeRefForBars(std::string_view source_ref) {
  if (source_ref.empty()) {
    return true;
  }
  if (source_ref[0] != '@') {
    return true;
  }
  return source_ref == "@symbol";
}

std::string DatasetSourceFromPlan(const JsonValue& plan) {
  return JsonStringAt(plan, "/outputs/dataset/source", "bars");
}

bool Layer3AugmentationRequested(const JsonValue& plan) {
  return sre::PointerExists(plan, "/outputs/layer3/outcomes") || sre::PointerExists(plan, "/outputs/layer3/bucketing") ||
         sre::PointerExists(plan, "/outputs/layer3/metrics") ||
         sre::PointerExists(plan, "/outputs/layer3/artifacts/outcomes_per_event") ||
         sre::PointerExists(plan, "/outputs/layer3/artifacts/bucket_stats");
}

bool AppendArtifactEmissionNotImplementedDiagnostics(const JsonValue& plan, sre::layers::DiagnosticSink& sink) {
  bool ok = true;
  auto emit = [&](std::string code, std::string check_id, std::string reason, std::string actual, std::string pointer) {
    sre::layers::EmitLayerError(
        sink,
        "outputs_repro",
        std::move(code),
        std::move(check_id),
        "CHKGRP_PLAN_AST_SHAPE",
        std::move(reason),
        "implemented runtime feature",
        std::move(actual),
        {std::move(pointer)},
        "Implement runtime execution path for this feature before enabling artifact emission.",
        "runtime");
    ok = false;
  };

  const std::string dataset_source = DatasetSourceFromPlan(plan);
  const auto* field_array = sre::ResolvePointer(plan, "/outputs/dataset/fields");
  if (dataset_source == "layer2_event_emitter") {
    std::set<std::string> emitted_columns = {"symbol", "bar_index"};
    const auto* emit_cols = sre::ResolvePointer(plan, "/execution/layer2/event_emitter/emit_columns");
    if (emit_cols && emit_cols->IsArray()) {
      for (const auto& c : emit_cols->AsArray().items) {
        if (!c.IsObject()) {
          continue;
        }
        const auto* name_v = ObjectGet(c, "name");
        if (name_v && name_v->IsString() && !name_v->AsString().empty()) {
          emitted_columns.insert(name_v->AsString());
        }
      }
    }

    std::set<std::string> missing_event_columns;
    if (field_array && field_array->IsArray()) {
      for (size_t i = 0; i < field_array->AsArray().items.size(); ++i) {
        const auto& field = field_array->AsArray().items[i];
        if (!field.IsObject()) {
          continue;
        }
        const std::string field_name = JsonStringAt(field, "/name", "field_" + std::to_string(i));
        const auto* ref = ObjectGet(field, "ref");
        const auto* from = ObjectGet(field, "from");
        const auto check_source = [&](const JsonValue* value, std::string_view key_name) {
          if (!value || !value->IsString()) {
            return;
          }
          const std::string src = value->AsString();
          if (src.empty() || src[0] != '@') {
            return;
          }
          if (src.rfind("@event.", 0) != 0) {
            sre::layers::EmitLayerError(
                sink,
                "outputs_repro",
                "E_DATASET_REF_SCOPE_MISMATCH",
                "CHK_DATASET_REF_SCOPE_MISMATCH",
                "CHKGRP_PLAN_AST_SHAPE",
                "Dataset field ref namespace does not match outputs.dataset.source=layer2_event_emitter.",
                "ref namespace=@event.*",
                "dataset_source=" + dataset_source + ", field=" + field_name + ", actual_ref=" + src,
                {"/outputs/dataset/source", "/outputs/dataset/fields/" + std::to_string(i) + "/" + std::string(key_name)},
                "Use @event.<column> and emit the column via execution.layer2.event_emitter.emit_columns",
                "runtime");
            ok = false;
            return;
          }
          const std::string col = src.substr(7);
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
                "runtime");
            ok = false;
          }
        };
        check_source(ref, "ref");
        check_source(from, "from");
      }
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
          "runtime");
      ok = false;
    }
  } else if (dataset_source == "bars") {
    if (field_array && field_array->IsArray()) {
      for (size_t i = 0; i < field_array->AsArray().items.size(); ++i) {
        const auto& field = field_array->AsArray().items[i];
        if (!field.IsObject()) {
          continue;
        }
        const auto* ref = ObjectGet(field, "ref");
        const auto* from = ObjectGet(field, "from");
        if (ref && ref->IsString() && !IsImplementedDatasetRuntimeRefForBars(ref->AsString())) {
          emit(
              "E_NOT_IMPLEMENTED",
              "CHK_RUNTIME_DATASET_REF_NOT_IMPLEMENTED",
              "Dataset field reference is declared but runtime execution is not implemented for bars source.",
              ref->AsString(),
              "/outputs/dataset/fields/" + std::to_string(i) + "/ref");
        }
        if (from && from->IsString() && !IsImplementedDatasetRuntimeRefForBars(from->AsString())) {
          emit(
              "E_NOT_IMPLEMENTED",
              "CHK_RUNTIME_DATASET_REF_NOT_IMPLEMENTED",
              "Dataset field source is declared but runtime execution is not implemented for bars source.",
              from->AsString(),
              "/outputs/dataset/fields/" + std::to_string(i) + "/from");
        }
      }
    }
  } else if (dataset_source != "layer2_event_emitter") {
    emit(
        "E_NOT_IMPLEMENTED",
        "CHK_RUNTIME_DATASET_SOURCE_NOT_IMPLEMENTED",
        "Dataset source is unknown to runtime.",
        dataset_source,
        "/outputs/dataset/source");
  }

  const bool symbol_resolution_strict = JsonBoolAt(
      plan, "/execution/backend/sierra_chart/layout_contract/readiness/symbol_resolution_strict", false);
  const auto* symbols_v = sre::ResolvePointer(plan, "/universe/symbols");
  const auto* workers_v = sre::ResolvePointer(plan, "/execution/worker_charts");
  const bool has_symbols = symbols_v && symbols_v->IsArray() && !symbols_v->AsArray().items.empty();
  const bool has_workers = workers_v && workers_v->IsArray() && !workers_v->AsArray().items.empty();
  if (has_symbols && !has_workers) {
    if (symbol_resolution_strict) {
      emit(
          "E_RUN_SYMBOL_CHART_NOT_FOUND_STRICT",
          "CHK_SYMBOL_CHART_RESOLUTION_STRICT",
          "Strict symbol resolution is enabled but execution.worker_charts is empty, so symbol->chart resolution is not deterministic.",
          "strict=true, resolved_chart=null, expected_worker_charts=[]",
          "/execution/worker_charts");
    } else {
      EmitRuntimeFeatureWarning(
          sink,
          "E_RUN_SYMBOL_CHART_NOT_FOUND",
          "CHK_SYMBOL_CHART_RESOLUTION_STRICT",
          "symbol_resolution_strict=false and worker chart mapping is absent; symbol resolution may be non-deterministic.",
          "deterministic symbol->chart mapping",
          "strict=false, expected_worker_charts=[]",
          {"/universe/symbols", "/execution/worker_charts"},
          "Set readiness.symbol_resolution_strict=true to fail fast, or configure worker charts deterministically.");
    }
  }

  const auto* layer3 = sre::ResolvePointer(plan, "/outputs/layer3");
  if (layer3 && layer3->IsObject()) {
    const auto* enabled = ObjectGet(*layer3, "enabled");
    const bool layer3_enabled = (enabled == nullptr) || !enabled->IsBool() || enabled->AsBool();
    if (layer3_enabled && !Layer3AugmentationRequested(plan)) {
      emit(
          "E_NOT_IMPLEMENTED",
          "CHK_RUNTIME_LAYER3_OUTPUT_NOT_IMPLEMENTED",
          "Layer3 output artifact emission is declared but not implemented in runtime.",
          "outputs.layer3.enabled=true",
          "/outputs/layer3");
    }
  }

  return ok;
}

std::string ResolveDatasetValueForBars(std::string_view source_ref, std::string_view symbol) {
  if (source_ref == "@symbol") {
    return std::string(symbol);
  }
  if (!source_ref.empty() && source_ref[0] == '@') {
    throw std::runtime_error("E_NOT_IMPLEMENTED: dataset runtime reference is not implemented: " + std::string(source_ref));
  }
  return std::string(source_ref);
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
    const std::vector<DatasetFieldSpec>& fields,
    const std::vector<std::vector<std::string>>& rows) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path);
  if (!out) {
    throw std::runtime_error("Failed to write JSONL file: " + path.string());
  }
  for (const auto& row : rows) {
    JsonValue::Object obj;
    for (size_t i = 0; i < fields.size(); ++i) {
      const std::string value = i < row.size() ? row[i] : "";
      obj.fields[fields[i].name] = value;
    }
    out << DumpCanonicalJson(JsonValue(std::move(obj))) << "\n";
  }
}

std::vector<std::string> ParseCsvLine(std::string_view line) {
  std::vector<std::string> cells;
  std::string current;
  bool in_quotes = false;
  for (size_t i = 0; i < line.size(); ++i) {
    const char ch = line[i];
    if (in_quotes) {
      if (ch == '"') {
        if (i + 1 < line.size() && line[i + 1] == '"') {
          current.push_back('"');
          ++i;
        } else {
          in_quotes = false;
        }
      } else {
        current.push_back(ch);
      }
    } else {
      if (ch == '"') {
        in_quotes = true;
      } else if (ch == ',') {
        cells.push_back(current);
        current.clear();
      } else {
        current.push_back(ch);
      }
    }
  }
  cells.push_back(current);
  return cells;
}

bool ReadCsvFile(const std::filesystem::path& path, std::vector<std::string>& headers, std::vector<std::vector<std::string>>& rows) {
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
      continue;
    }
    if (!cells.empty()) {
      rows.push_back(std::move(cells));
    }
  }
  return !headers.empty();
}

struct SyntheticBar {
  double open = 0.0;
  double high = 0.0;
  double low = 0.0;
  double close = 0.0;
  double volume = 0.0;
  double vwap = 0.0;
  std::string datetime;
};

uint32_t StableSymbolSeed(std::string_view symbol) {
  uint32_t seed = 2166136261u;
  for (unsigned char c : symbol) {
    seed ^= static_cast<uint32_t>(c);
    seed *= 16777619u;
  }
  return seed;
}

std::vector<SyntheticBar> BuildSyntheticBars(std::string_view symbol, size_t bar_count = 128) {
  std::vector<SyntheticBar> bars;
  bars.reserve(bar_count);
  const uint32_t seed = StableSymbolSeed(symbol);
  const double base = 40.0 + static_cast<double>(seed % 250) / 5.0;

  for (size_t i = 0; i < bar_count; ++i) {
    const double trend = static_cast<double>(i) * 0.09;
    const double wave = std::sin((static_cast<double>(i) + static_cast<double>(seed % 19)) * 0.25) * 0.8;
    const double close = base + trend + wave;
    const double open = close - 0.25 + std::cos(static_cast<double>(i) * 0.15) * 0.05;
    const double high = std::max(open, close) + 0.45 + static_cast<double>((seed + i) % 7) * 0.01;
    const double low = std::min(open, close) - 0.40 - static_cast<double>((seed + i * 3) % 5) * 0.01;
    const double volume = 1000.0 + static_cast<double>((seed + i * 17) % 700);
    const double vwap = (open + high + low + close) / 4.0;

    const int total_minutes = static_cast<int>(i * 5);
    const int day = 1 + (total_minutes / (24 * 60));
    const int hour = (total_minutes / 60) % 24;
    const int minute = total_minutes % 60;
    std::ostringstream dt;
    dt << "2025-01-" << std::setw(2) << std::setfill('0') << day << "T" << std::setw(2) << hour << ":" << std::setw(2)
       << minute << ":00Z";

    bars.push_back(SyntheticBar{
        .open = open,
        .high = high,
        .low = low,
        .close = close,
        .volume = volume,
        .vwap = vwap,
        .datetime = dt.str(),
    });
  }
  return bars;
}

double BarFieldAsNumber(const SyntheticBar& bar, std::string_view field) {
  if (field == "open") {
    return bar.open;
  }
  if (field == "high") {
    return bar.high;
  }
  if (field == "low") {
    return bar.low;
  }
  if (field == "close") {
    return bar.close;
  }
  if (field == "volume") {
    return bar.volume;
  }
  if (field == "vwap") {
    return bar.vwap;
  }
  return bar.close;
}

enum class L2ValueKind { Number, Bool, String };

struct L2Value {
  L2ValueKind kind = L2ValueKind::Number;
  double number = 0.0;
  bool boolean = false;
  std::string text;
};

L2Value MakeNumber(double value) { return L2Value{.kind = L2ValueKind::Number, .number = value}; }
L2Value MakeBool(bool value) { return L2Value{.kind = L2ValueKind::Bool, .number = value ? 1.0 : 0.0, .boolean = value}; }
L2Value MakeString(std::string value) { return L2Value{.kind = L2ValueKind::String, .text = std::move(value)}; }

double L2ToNumber(const L2Value& value) {
  if (value.kind == L2ValueKind::Number) {
    return value.number;
  }
  if (value.kind == L2ValueKind::Bool) {
    return value.boolean ? 1.0 : 0.0;
  }
  double parsed = 0.0;
  if (ParseDoubleStrict(value.text, parsed)) {
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
  if (value.text == "true") {
    return true;
  }
  if (value.text == "false") {
    return false;
  }
  return !value.text.empty();
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

bool ParseStudyRef(std::string_view source_ref, std::string& study_key, std::string& output_key) {
  static constexpr std::string_view kPrefix = "@study.";
  if (source_ref.rfind(kPrefix, 0) != 0) {
    return false;
  }
  const std::string tail(source_ref.substr(kPrefix.size()));
  const size_t dot = tail.find('.');
  if (dot == std::string::npos) {
    return false;
  }
  study_key = tail.substr(0, dot);
  output_key = tail.substr(dot + 1);
  return !study_key.empty() && !output_key.empty();
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

std::vector<L2Value> BuildStudyRefSeries(
    const JsonValue& plan,
    std::string_view ref,
    const std::vector<SyntheticBar>& bars,
    sre::layers::DiagnosticSink& sink,
    const std::string& pointer) {
  std::string study_key;
  std::string output_key;
  if (!ParseStudyRef(ref, study_key, output_key)) {
    EmitRuntimeFeatureError(
        sink,
        "E_L2_MISSING_NODE_DEP",
        "CHK_L2_STUDY_REF_RESOLVE",
        "Layer2 study_ref node must resolve @study.<key>.<output>.",
        "@study.<key>.<output>",
        std::string(ref),
        pointer,
        "Use a valid study_ref that points to an existing studies entry.");
    return {};
  }

  const auto* studies = sre::ResolvePointer(plan, "/studies");
  const auto* study = studies && studies->IsObject() ? ObjectGet(*studies, study_key) : nullptr;
  const auto* outputs = study && study->IsObject() ? ObjectGet(*study, "outputs") : nullptr;
  const auto* output = outputs && outputs->IsObject() ? ObjectGet(*outputs, output_key) : nullptr;
  if (!study || !outputs || !output) {
    EmitRuntimeFeatureError(
        sink,
        "E_L2_MISSING_NODE_DEP",
        "CHK_L2_STUDY_REF_RESOLVE",
        "Layer2 study_ref references a missing studies binding.",
        "existing /studies/<key>/outputs/<output>",
        std::string(ref),
        pointer,
        "Declare the referenced study output in plan.studies before using study_ref.");
    return {};
  }

  const uint32_t ref_seed = StableSymbolSeed(ref);
  std::vector<L2Value> series;
  series.reserve(bars.size());
  for (size_t i = 0; i < bars.size(); ++i) {
    const double scale = 1.0 + static_cast<double>((ref_seed + i) % 17) / 200.0;
    series.push_back(MakeNumber(bars[i].close * scale));
  }
  return series;
}

bool BuildLayer2TopoOrder(
    const JsonValue::Object& nodes,
    std::vector<std::string>& order,
    sre::layers::DiagnosticSink& sink) {
  std::unordered_map<std::string, int> indegree;
  std::unordered_map<std::string, std::vector<std::string>> adjacency;
  bool ok = true;

  for (const auto& [node_id, _] : nodes.fields) {
    indegree[node_id] = 0;
  }

  for (const auto& [node_id, node_value] : nodes.fields) {
    if (!node_value.IsObject()) {
      EmitRuntimeFeatureError(
          sink,
          "E_L2_UNKNOWN_NODE_KIND",
          "CHK_L2_NODE_KIND",
          "Layer2 node must be an object.",
          "object",
          node_value.TypeName(),
          "/execution/layer2/indicator_dag/nodes/" + node_id,
          "Define each Layer2 DAG node as an object with a valid kind.");
      ok = false;
      continue;
    }

    const auto* kind_v = ObjectGet(node_value, "kind");
    const std::string kind = kind_v && kind_v->IsString() ? kind_v->AsString() : "";
    if (!IsKnownLayer2NodeKind(kind)) {
      EmitRuntimeFeatureError(
          sink,
          "E_L2_UNKNOWN_NODE_KIND",
          "CHK_L2_NODE_KIND",
          "Layer2 node kind is unknown.",
          "param|bar_field|study_ref|supported op kind",
          kind.empty() ? JsonTypeName(kind_v) : kind,
          "/execution/layer2/indicator_dag/nodes/" + node_id + "/kind",
          "Use a supported node kind.");
      ok = false;
      continue;
    }

    if (!IsKnownLayer2Op(kind)) {
      continue;
    }

    const auto* inputs = ObjectGet(node_value, "inputs");
    if (!inputs || !inputs->IsArray() || inputs->AsArray().items.empty()) {
      EmitRuntimeFeatureError(
          sink,
          "E_L2_MISSING_NODE_DEP",
          "CHK_L2_NODE_DEP",
          "Layer2 op node must declare at least one input dependency.",
          "inputs array[minItems=1]",
          JsonTypeName(inputs),
          "/execution/layer2/indicator_dag/nodes/" + node_id + "/inputs",
          "Declare one or more input node ids for the operation.");
      ok = false;
      continue;
    }

    for (size_t i = 0; i < inputs->AsArray().items.size(); ++i) {
      const auto& dep = inputs->AsArray().items[i];
      if (!dep.IsString() || dep.AsString().empty()) {
        EmitRuntimeFeatureError(
            sink,
            "E_L2_MISSING_NODE_DEP",
            "CHK_L2_NODE_DEP",
            "Layer2 op dependency entry must be a non-empty node id string.",
            "existing node id",
            dep.TypeName(),
            "/execution/layer2/indicator_dag/nodes/" + node_id + "/inputs/" + std::to_string(i),
            "Replace dependency with an existing node id.");
        ok = false;
        continue;
      }
      if (!indegree.contains(dep.AsString())) {
        EmitRuntimeFeatureError(
            sink,
            "E_L2_MISSING_NODE_DEP",
            "CHK_L2_NODE_DEP",
            "Layer2 op dependency is missing from indicator_dag.nodes.",
            "declared dependency node",
            dep.AsString(),
            "/execution/layer2/indicator_dag/nodes/" + node_id + "/inputs/" + std::to_string(i),
            "Declare the dependency node in execution.layer2.indicator_dag.nodes.");
        ok = false;
        continue;
      }
      adjacency[dep.AsString()].push_back(node_id);
      indegree[node_id] += 1;
    }
  }

  if (!ok) {
    return false;
  }

  std::set<std::string, std::less<>> ready;
  for (const auto& [node_id, deg] : indegree) {
    if (deg == 0) {
      ready.insert(node_id);
    }
  }

  while (!ready.empty()) {
    const std::string current = *ready.begin();
    ready.erase(ready.begin());
    order.push_back(current);
    auto next_it = adjacency.find(current);
    if (next_it == adjacency.end()) {
      continue;
    }
    std::sort(next_it->second.begin(), next_it->second.end());
    for (const auto& next : next_it->second) {
      auto deg_it = indegree.find(next);
      if (deg_it == indegree.end()) {
        continue;
      }
      deg_it->second -= 1;
      if (deg_it->second == 0) {
        ready.insert(next);
      }
    }
  }

  if (order.size() != nodes.fields.size()) {
    EmitRuntimeFeatureError(
        sink,
        "E_L2_DAG_CYCLE",
        "CHK_L2_DAG_CYCLE",
        "Layer2 indicator_dag contains a cycle.",
        "acyclic DAG",
        "cycle detected",
        "/execution/layer2/indicator_dag/nodes",
        "Remove cyclic dependencies from execution.layer2.indicator_dag.nodes.");
    return false;
  }

  return true;
}

L2Value EvaluateLayer2OpAt(
    std::string_view op_kind,
    const std::vector<std::vector<L2Value>>& inputs,
    size_t bar_index,
    int window_bars) {
  auto input_at = [&](size_t input_index, size_t idx) -> const L2Value& {
    static const L2Value kDefault = MakeNumber(0.0);
    if (input_index >= inputs.size()) {
      return kDefault;
    }
    if (idx >= inputs[input_index].size()) {
      return kDefault;
    }
    return inputs[input_index][idx];
  };

  if (op_kind == "sma") {
    const int window = std::max(1, window_bars);
    const size_t start = bar_index >= static_cast<size_t>(window - 1) ? bar_index - static_cast<size_t>(window - 1) : 0;
    double sum = 0.0;
    size_t count = 0;
    for (size_t i = start; i <= bar_index; ++i) {
      sum += L2ToNumber(input_at(0, i));
      ++count;
    }
    return MakeNumber(count == 0 ? 0.0 : sum / static_cast<double>(count));
  }
  if (op_kind == "gt") {
    return MakeBool(L2ToNumber(input_at(0, bar_index)) > L2ToNumber(input_at(1, bar_index)));
  }
  if (op_kind == "and") {
    bool value = true;
    for (size_t i = 0; i < inputs.size(); ++i) {
      value = value && L2ToBool(input_at(i, bar_index));
    }
    return MakeBool(value);
  }
  if (op_kind == "or") {
    bool value = false;
    for (size_t i = 0; i < inputs.size(); ++i) {
      value = value || L2ToBool(input_at(i, bar_index));
    }
    return MakeBool(value);
  }
  if (op_kind == "not") {
    return MakeBool(!L2ToBool(input_at(0, bar_index)));
  }
  if (op_kind == "gte") {
    return MakeBool(L2ToNumber(input_at(0, bar_index)) >= L2ToNumber(input_at(1, bar_index)));
  }
  if (op_kind == "lt") {
    return MakeBool(L2ToNumber(input_at(0, bar_index)) < L2ToNumber(input_at(1, bar_index)));
  }
  if (op_kind == "lte") {
    return MakeBool(L2ToNumber(input_at(0, bar_index)) <= L2ToNumber(input_at(1, bar_index)));
  }
  if (op_kind == "eq") {
    return MakeBool(L2ToString(input_at(0, bar_index)) == L2ToString(input_at(1, bar_index)));
  }
  if (op_kind == "neq") {
    return MakeBool(L2ToString(input_at(0, bar_index)) != L2ToString(input_at(1, bar_index)));
  }
  if (op_kind == "add") {
    return MakeNumber(L2ToNumber(input_at(0, bar_index)) + L2ToNumber(input_at(1, bar_index)));
  }
  if (op_kind == "sub" || op_kind == "diff") {
    return MakeNumber(L2ToNumber(input_at(0, bar_index)) - L2ToNumber(input_at(1, bar_index)));
  }
  if (op_kind == "mul") {
    return MakeNumber(L2ToNumber(input_at(0, bar_index)) * L2ToNumber(input_at(1, bar_index)));
  }
  if (op_kind == "div") {
    const double denom = L2ToNumber(input_at(1, bar_index));
    if (std::fabs(denom) < 1e-12) {
      return MakeNumber(0.0);
    }
    return MakeNumber(L2ToNumber(input_at(0, bar_index)) / denom);
  }
  if (op_kind == "abs") {
    return MakeNumber(std::fabs(L2ToNumber(input_at(0, bar_index))));
  }
  return MakeNumber(0.0);
}

std::string CoerceEmitType(std::string_view value, std::string_view type_name) {
  if (type_name == "string" || type_name == "datetime") {
    return std::string(value);
  }
  if (type_name == "bool") {
    if (value == "true" || value == "1") {
      return "true";
    }
    return "false";
  }
  if (type_name == "int") {
    int parsed = 0;
    if (ParseIntStrict(value, parsed)) {
      return std::to_string(parsed);
    }
    double as_double = 0.0;
    if (ParseDoubleStrict(value, as_double)) {
      return std::to_string(static_cast<int>(std::llround(as_double)));
    }
    return "0";
  }
  if (type_name == "float") {
    double parsed = 0.0;
    if (ParseDoubleStrict(value, parsed)) {
      return FormatNumber(parsed);
    }
    return "0";
  }
  return std::string(value);
}

struct Layer2EventRow {
  std::string symbol;
  int bar_index = 0;
  std::unordered_map<std::string, std::string> columns;
};

struct Layer2EvaluationResult {
  bool ok = true;
  std::unordered_map<std::string, std::vector<Layer2EventRow>> events_by_symbol;
  std::unordered_map<std::string, std::vector<SyntheticBar>> bars_by_symbol;
};

struct Layer2RuntimeConfig {
  bool enabled = false;
  std::string bar_basis = "close";
  bool require_final_bar = true;
  std::string align_mode = "last_completed";
};

Layer2RuntimeConfig ParseLayer2RuntimeConfig(const JsonValue& plan) {
  Layer2RuntimeConfig cfg;
  const auto* layer2 = sre::ResolvePointer(plan, "/execution/layer2");
  if (!layer2 || !layer2->IsObject()) {
    return cfg;
  }
  cfg.enabled = JsonBoolAt(plan, "/execution/layer2/enabled", true);
  cfg.bar_basis = JsonStringAt(plan, "/execution/layer2/bar_basis", "close");
  if (cfg.bar_basis != "close" && cfg.bar_basis != "open" && cfg.bar_basis != "intrabar") {
    cfg.bar_basis = "close";
  }
  cfg.require_final_bar = JsonBoolAt(plan, "/execution/layer2/require_final_bar", true);
  cfg.align_mode = JsonStringAt(plan, "/execution/layer2/align/mode", "last_completed");
  if (cfg.align_mode != "last_completed" && cfg.align_mode != "strict_close") {
    cfg.align_mode = "last_completed";
  }
  return cfg;
}

Layer2EvaluationResult EvaluateLayer2(const JsonValue& plan, const std::vector<std::string>& symbols, sre::layers::DiagnosticSink& sink) {
  Layer2EvaluationResult result;
  const Layer2RuntimeConfig layer2_cfg = ParseLayer2RuntimeConfig(plan);
  if (!layer2_cfg.enabled) {
    return result;
  }

  const auto* nodes_value = sre::ResolvePointer(plan, "/execution/layer2/indicator_dag/nodes");
  if (!nodes_value || !nodes_value->IsObject() || nodes_value->AsObject().fields.empty()) {
    EmitRuntimeFeatureError(
        sink,
        "E_L2_MISSING_NODE_DEP",
        "CHK_L2_NODES_PRESENT",
        "Layer2 enabled requires indicator_dag.nodes to be a non-empty object.",
        "non-empty object",
        JsonTypeName(nodes_value),
        "/execution/layer2/indicator_dag/nodes",
        "Define execution.layer2.indicator_dag.nodes.");
    result.ok = false;
    return result;
  }

  std::vector<std::string> topo_order;
  if (!BuildLayer2TopoOrder(nodes_value->AsObject(), topo_order, sink)) {
    result.ok = false;
    return result;
  }

  const auto* trigger_node_value = sre::ResolvePointer(plan, "/execution/layer2/event_emitter/trigger_node");
  const std::string trigger_node =
      trigger_node_value && trigger_node_value->IsString() ? trigger_node_value->AsString() : std::string();
  if (trigger_node.empty() || !nodes_value->AsObject().fields.contains(trigger_node)) {
    EmitRuntimeFeatureError(
        sink,
        "E_L2_MISSING_NODE_DEP",
        "CHK_L2_TRIGGER_NODE",
        "Layer2 event_emitter.trigger_node must reference an existing DAG node.",
        "existing node id",
        trigger_node.empty() ? JsonTypeName(trigger_node_value) : trigger_node,
        "/execution/layer2/event_emitter/trigger_node",
        "Set event_emitter.trigger_node to a declared node id.");
    result.ok = false;
    return result;
  }

  const std::string emit_mode = JsonStringAt(plan, "/execution/layer2/event_emitter/emit_mode", "on_true_edge");
  int cooldown = 0;
  const auto* cooldown_v = sre::ResolvePointer(plan, "/execution/layer2/event_emitter/cooldown_bars");
  if (cooldown_v && cooldown_v->IsNumber()) {
    cooldown = std::max(0, static_cast<int>(std::llround(cooldown_v->AsNumber())));
  }
  const PlanDateRange date_range = ParsePlanDateRange(plan);
  const auto* emit_columns = sre::ResolvePointer(plan, "/execution/layer2/event_emitter/emit_columns");
  if (!emit_columns || !emit_columns->IsArray() || emit_columns->AsArray().items.empty()) {
    EmitRuntimeFeatureError(
        sink,
        "E_L2_MISSING_NODE_DEP",
        "CHK_L2_EMIT_COLUMNS",
        "Layer2 event emitter requires emit_columns.",
        "emit_columns array[minItems=1]",
        JsonTypeName(emit_columns),
        "/execution/layer2/event_emitter/emit_columns",
        "Provide one or more output columns in execution.layer2.event_emitter.emit_columns.");
    result.ok = false;
    return result;
  }

  for (const auto& symbol : symbols) {
    auto bars = BuildSyntheticBars(symbol);
    std::unordered_map<std::string, std::vector<L2Value>> node_values;
    bool symbol_ok = true;

    for (const auto& node_id : topo_order) {
      const auto& node = nodes_value->AsObject().fields.at(node_id);
      const auto* kind_v = ObjectGet(node, "kind");
      const std::string kind = kind_v && kind_v->IsString() ? kind_v->AsString() : "";
      std::vector<L2Value> series;
      series.reserve(bars.size());

      if (kind == "param") {
        const auto* type_v = ObjectGet(node, "type");
        const auto* value_v = ObjectGet(node, "value");
        const std::string param_type = type_v && type_v->IsString() ? type_v->AsString() : "float";
        L2Value value = MakeNumber(0.0);
        if (param_type == "bool") {
          value = MakeBool(value_v && value_v->IsBool() ? value_v->AsBool() : false);
        } else if (param_type == "string") {
          value = MakeString(value_v && value_v->IsString() ? value_v->AsString() : "");
        } else {
          value = MakeNumber(value_v && value_v->IsNumber() ? value_v->AsNumber() : 0.0);
        }
        series.assign(bars.size(), value);
      } else if (kind == "bar_field") {
        const auto* field_v = ObjectGet(node, "field");
        std::string default_field = "close";
        if (layer2_cfg.bar_basis == "open") {
          default_field = "open";
        }
        const std::string field = field_v && field_v->IsString() ? field_v->AsString() : default_field;
        int lag = 0;
        const auto* lag_v = ObjectGet(node, "lag_bars");
        if (lag_v && lag_v->IsNumber()) {
          lag = std::max(0, static_cast<int>(std::llround(lag_v->AsNumber())));
        }
        for (size_t i = 0; i < bars.size(); ++i) {
          const size_t src = i >= static_cast<size_t>(lag) ? i - static_cast<size_t>(lag) : 0;
          if (field == "datetime") {
            series.push_back(MakeString(bars[src].datetime));
          } else {
            series.push_back(MakeNumber(BarFieldAsNumber(bars[src], field)));
          }
        }
      } else if (kind == "study_ref") {
        const auto* ref_v = ObjectGet(node, "ref");
        const std::string ref = ref_v && ref_v->IsString() ? ref_v->AsString() : "";
        int lag = 0;
        const auto* lag_v = ObjectGet(node, "lag_bars");
        if (lag_v && lag_v->IsNumber()) {
          lag = std::max(0, static_cast<int>(std::llround(lag_v->AsNumber())));
        }
        auto raw = BuildStudyRefSeries(plan, ref, bars, sink, "/execution/layer2/indicator_dag/nodes/" + node_id + "/ref");
        if (raw.empty()) {
          symbol_ok = false;
          break;
        }
        for (size_t i = 0; i < raw.size(); ++i) {
          const size_t src = i >= static_cast<size_t>(lag) ? i - static_cast<size_t>(lag) : 0;
          series.push_back(raw[src]);
        }
      } else if (IsKnownLayer2Op(kind)) {
        const auto* inputs_v = ObjectGet(node, "inputs");
        const auto* window_v = ObjectGet(node, "window_bars");
        const int window = window_v && window_v->IsNumber() ? std::max(1, static_cast<int>(std::llround(window_v->AsNumber()))) : 1;
        std::vector<std::vector<L2Value>> inputs;
        if (inputs_v && inputs_v->IsArray()) {
          for (size_t i = 0; i < inputs_v->AsArray().items.size(); ++i) {
            const auto& dep = inputs_v->AsArray().items[i];
            if (!dep.IsString()) {
              continue;
            }
            auto dep_it = node_values.find(dep.AsString());
            if (dep_it == node_values.end()) {
              EmitRuntimeFeatureError(
                  sink,
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
        for (size_t i = 0; i < bars.size(); ++i) {
          series.push_back(EvaluateLayer2OpAt(kind, inputs, i, window));
        }
      }
      node_values[node_id] = std::move(series);
    }

    if (!symbol_ok) {
      result.ok = false;
      break;
    }

    std::vector<Layer2EventRow> events;
    bool prev_trigger = false;
    int last_emit = -1000000;
    size_t evaluable_bars = bars.size();
    const bool completed_only =
        layer2_cfg.require_final_bar && (layer2_cfg.align_mode == "last_completed" || layer2_cfg.align_mode == "strict_close");
    if (completed_only && evaluable_bars > 0) {
      evaluable_bars -= 1;
    }
    for (size_t i = 0; i < evaluable_bars; ++i) {
      const bool trigger = L2ToBool(node_values[trigger_node][i]);
      if (!DateInRange(bars[i].datetime, date_range)) {
        prev_trigger = trigger;
        continue;
      }
      bool fire = emit_mode == "on_true" ? trigger : (trigger && !prev_trigger);
      if (fire && cooldown > 0 && static_cast<int>(i) - last_emit <= cooldown) {
        fire = false;
      }
      if (fire) {
        Layer2EventRow row;
        row.symbol = symbol;
        row.bar_index = static_cast<int>(i);
        row.columns["symbol"] = symbol;
        row.columns["bar_index"] = std::to_string(row.bar_index);
        for (size_t c = 0; c < emit_columns->AsArray().items.size(); ++c) {
          const auto& col = emit_columns->AsArray().items[c];
          if (!col.IsObject()) {
            continue;
          }
          const auto* name_v = ObjectGet(col, "name");
          if (!name_v || !name_v->IsString() || name_v->AsString().empty()) {
            continue;
          }
          const std::string name = name_v->AsString();
          const std::string type = JsonStringAt(col, "/type", "string");
          std::string raw;

          const auto* ref_v = ObjectGet(col, "ref");
          if (ref_v && ref_v->IsString()) {
            const std::string ref = ref_v->AsString();
            if (ref == "@symbol") raw = symbol;
            else if (ref == "@bar.open") raw = FormatNumber(bars[i].open);
            else if (ref == "@bar.high") raw = FormatNumber(bars[i].high);
            else if (ref == "@bar.low") raw = FormatNumber(bars[i].low);
            else if (ref == "@bar.close") raw = FormatNumber(bars[i].close);
            else if (ref == "@bar.volume") raw = FormatNumber(bars[i].volume);
            else if (ref == "@bar.vwap") raw = FormatNumber(bars[i].vwap);
            else if (ref == "@bar.datetime") raw = bars[i].datetime;
          }
          const auto* node_v = ObjectGet(col, "node");
          if (raw.empty() && node_v && node_v->IsString()) {
            auto it = node_values.find(node_v->AsString());
            if (it == node_values.end() || i >= it->second.size()) {
              EmitRuntimeFeatureError(
                  sink,
                  "E_L2_MISSING_NODE_DEP",
                  "CHK_L2_EMIT_NODE",
                  "event_emitter column node reference is missing.",
                  "existing node id",
                  node_v->AsString(),
                  "/execution/layer2/event_emitter/emit_columns/" + std::to_string(c) + "/node",
                  "Reference an existing indicator_dag node.");
              symbol_ok = false;
              break;
            }
            raw = L2ToString(it->second[i]);
          }

          row.columns[name] = CoerceEmitType(raw, type);
        }
        if (!symbol_ok) {
          break;
        }
        events.push_back(std::move(row));
        last_emit = static_cast<int>(i);
      }
      prev_trigger = trigger;
    }
    if (!symbol_ok) {
      result.ok = false;
      break;
    }

    result.bars_by_symbol[symbol] = std::move(bars);
    result.events_by_symbol[symbol] = std::move(events);
  }

  return result;
}

std::string ResolveEventDatasetValue(
    const DatasetFieldSpec& field,
    const Layer2EventRow& event,
    const std::vector<SyntheticBar>& bars) {
  const std::string& source = field.source_ref;
  if (source.empty()) {
    auto it = event.columns.find(field.name);
    return it == event.columns.end() ? "" : it->second;
  }
  if (source.rfind("@event.", 0) == 0) {
    const std::string key = source.substr(7);
    auto it = event.columns.find(key);
    return it == event.columns.end() ? "" : it->second;
  }
  if (source == "@symbol") {
    return event.symbol;
  }
  if (source == "@bar.datetime") {
    return event.bar_index >= 0 && static_cast<size_t>(event.bar_index) < bars.size() ? bars[static_cast<size_t>(event.bar_index)].datetime : "";
  }
  if (source == "@bar.open" || source == "@bar.high" || source == "@bar.low" || source == "@bar.close" || source == "@bar.volume" ||
      source == "@bar.vwap") {
    if (event.bar_index < 0 || static_cast<size_t>(event.bar_index) >= bars.size()) {
      return "";
    }
    const auto& bar = bars[static_cast<size_t>(event.bar_index)];
    if (source == "@bar.open") {
      return FormatNumber(bar.open);
    }
    if (source == "@bar.high") {
      return FormatNumber(bar.high);
    }
    if (source == "@bar.low") {
      return FormatNumber(bar.low);
    }
    if (source == "@bar.close") {
      return FormatNumber(bar.close);
    }
    if (source == "@bar.volume") {
      return FormatNumber(bar.volume);
    }
    return FormatNumber(bar.vwap);
  }
  if (source.rfind("@node.", 0) == 0) {
    const std::string key = source.substr(6);
    auto it = event.columns.find(key);
    return it == event.columns.end() ? "" : it->second;
  }
  if (!source.empty() && source[0] == '@') {
    auto it = event.columns.find(source);
    return it == event.columns.end() ? "" : it->second;
  }
  return source;
}

bool EmitBarsDatasetLegacy(
    const JsonValue& plan,
    const std::filesystem::path& repo_root,
    const std::string& dataset_format,
    const std::string& raw_path,
    const std::vector<DatasetFieldSpec>& fields,
    sre::layers::DiagnosticSink& sink) {
  std::vector<std::string> headers;
  headers.reserve(fields.size());
  for (const auto& field : fields) {
    headers.push_back(field.name);
  }

  const std::vector<std::string> symbols = PlanUniverseSymbols(plan);
  auto build_row = [&](std::string_view symbol, bool& ok) {
    std::vector<std::string> row;
    row.reserve(fields.size());
    for (const auto& field : fields) {
      try {
        row.push_back(ResolveDatasetValueForBars(field.source_ref, symbol));
      } catch (const std::exception& ex) {
        EmitRuntimeFeatureError(
            sink,
            "E_NOT_IMPLEMENTED",
            "CHK_RUNTIME_DATASET_REF_NOT_IMPLEMENTED",
            "Dataset bars source reference is not implemented.",
            "@symbol or literal value",
            ex.what(),
            field.source_pointer.empty() ? "/outputs/dataset/fields" : field.source_pointer,
            "Keep outputs.dataset.source=bars with @symbol/literal refs, or switch to layer2_event_emitter.");
        ok = false;
        return std::vector<std::string>{};
      }
    }
    return row;
  };

  const bool per_symbol = PathTemplateUsesSymbol(raw_path);
  if (per_symbol) {
    for (const auto& symbol : symbols) {
      bool ok = true;
      const std::vector<std::vector<std::string>> rows{build_row(symbol, ok)};
      if (!ok) {
        return false;
      }
      const auto out_path = ResolvePlanOutputPath(raw_path, repo_root, symbol);
      if (dataset_format == "csv") {
        WriteCsvFile(out_path, headers, rows);
      } else if (dataset_format == "jsonl") {
        WriteJsonlFile(out_path, fields, rows);
      }
    }
    return true;
  }

  std::vector<std::vector<std::string>> rows;
  rows.reserve(symbols.size());
  for (const auto& symbol : symbols) {
    bool ok = true;
    auto row = build_row(symbol, ok);
    if (!ok) {
      return false;
    }
    rows.push_back(std::move(row));
  }
  const auto out_path = ResolvePlanOutputPath(raw_path, repo_root, "");
  if (dataset_format == "csv") {
    WriteCsvFile(out_path, headers, rows);
  } else if (dataset_format == "jsonl") {
    WriteJsonlFile(out_path, fields, rows);
  }
  return true;
}

bool EmitLayer2EventDataset(
    const JsonValue& plan,
    const std::filesystem::path& repo_root,
    const std::string& dataset_format,
    const std::string& raw_path,
    const std::vector<DatasetFieldSpec>& fields,
    sre::layers::DiagnosticSink& sink,
    std::unordered_map<std::string, std::vector<SyntheticBar>>& layer2_bars_by_symbol) {
  const std::vector<std::string> symbols = PlanUniverseSymbols(plan);
  Layer2EvaluationResult layer2 = EvaluateLayer2(plan, symbols, sink);
  if (!layer2.ok) {
    return false;
  }
  layer2_bars_by_symbol = layer2.bars_by_symbol;

  std::vector<std::string> headers;
  headers.reserve(fields.size());
  for (const auto& field : fields) {
    headers.push_back(field.name);
  }

  const PlanDateRange date_range = ParsePlanDateRange(plan);
  const bool enforce_l2_date_range = JsonBoolAt(plan, "/execution/layer2/enabled", false) && date_range.enabled;
  std::string emitted_min_dt = "9999-99-99";
  std::string emitted_max_dt = "0000-00-00";
  const auto observe_dt = [&](std::string_view dt) {
    if (dt.size() < 10) {
      return;
    }
    const std::string d(dt.substr(0, 10));
    emitted_min_dt = std::min(emitted_min_dt, d);
    emitted_max_dt = std::max(emitted_max_dt, d);
  };
  const auto event_dt = [&](const Layer2EventRow& event, const std::vector<SyntheticBar>& bars) {
    auto it = event.columns.find("bar_datetime");
    if (it != event.columns.end() && !it->second.empty()) {
      return it->second;
    }
    if (event.bar_index >= 0 && static_cast<size_t>(event.bar_index) < bars.size()) {
      return bars[static_cast<size_t>(event.bar_index)].datetime;
    }
    return std::string();
  };
  const auto enforce_event_dt = [&](const Layer2EventRow& event, const std::vector<SyntheticBar>& bars) -> bool {
    if (!enforce_l2_date_range) {
      return true;
    }
    const std::string dt = event_dt(event, bars);
    observe_dt(dt);
    if (DateInRange(dt, date_range)) {
      return true;
    }
    EmitRuntimeFeatureError(
        sink,
        "E_L2_DATE_RANGE_VIOLATION",
        "CHK_DATE_RANGE_ENFORCED_IN_L2",
        "L2 authoritative emission produced a row outside universe.date_range.",
        "rows constrained to date_range.start..date_range.end",
        "date_range.start=" + date_range.start_date + ", date_range.end=" + date_range.end_date + ", emitted_min_dt=" + emitted_min_dt +
            ", emitted_max_dt=" + emitted_max_dt + ", violating_dt=" + dt,
        "/universe/date_range",
        "Fix L2 emitter to filter bars by universe.date_range");
    return false;
  };

  const bool per_symbol = PathTemplateUsesSymbol(raw_path);
  if (per_symbol) {
    for (const auto& symbol : symbols) {
      const auto out_path = ResolvePlanOutputPath(raw_path, repo_root, symbol);
      std::vector<std::vector<std::string>> rows;
      const auto events_it = layer2.events_by_symbol.find(symbol);
      const auto bars_it = layer2.bars_by_symbol.find(symbol);
      if (events_it != layer2.events_by_symbol.end() && bars_it != layer2.bars_by_symbol.end()) {
        for (const auto& event : events_it->second) {
          if (!enforce_event_dt(event, bars_it->second)) {
            return false;
          }
          std::vector<std::string> row;
          row.reserve(fields.size());
          for (const auto& field : fields) {
            row.push_back(ResolveEventDatasetValue(field, event, bars_it->second));
          }
          rows.push_back(std::move(row));
        }
      }
      if (dataset_format == "csv") {
        WriteCsvFile(out_path, headers, rows);
      } else if (dataset_format == "jsonl") {
        WriteJsonlFile(out_path, fields, rows);
      }
    }
    return true;
  }

  std::vector<std::vector<std::string>> rows;
  for (const auto& symbol : symbols) {
    const auto events_it = layer2.events_by_symbol.find(symbol);
    const auto bars_it = layer2.bars_by_symbol.find(symbol);
    if (events_it == layer2.events_by_symbol.end() || bars_it == layer2.bars_by_symbol.end()) {
      continue;
    }
    for (const auto& event : events_it->second) {
      if (!enforce_event_dt(event, bars_it->second)) {
        return false;
      }
      std::vector<std::string> row;
      row.reserve(fields.size());
      for (const auto& field : fields) {
        row.push_back(ResolveEventDatasetValue(field, event, bars_it->second));
      }
      rows.push_back(std::move(row));
    }
  }
  const auto out_path = ResolvePlanOutputPath(raw_path, repo_root, "");
  if (dataset_format == "csv") {
    WriteCsvFile(out_path, headers, rows);
  } else if (dataset_format == "jsonl") {
    WriteJsonlFile(out_path, fields, rows);
  }
  return true;
}

double QuantileValue(std::vector<double> values, double q) {
  if (values.empty()) {
    return 0.0;
  }
  std::sort(values.begin(), values.end());
  const double clamped_q = std::max(0.0, std::min(1.0, q));
  const double pos = clamped_q * static_cast<double>(values.size() - 1);
  const size_t lo = static_cast<size_t>(std::floor(pos));
  const size_t hi = static_cast<size_t>(std::ceil(pos));
  if (lo == hi) {
    return values[lo];
  }
  const double alpha = pos - static_cast<double>(lo);
  return values[lo] * (1.0 - alpha) + values[hi] * alpha;
}

bool ComputeLayer3Augmentation(
    const JsonValue& plan,
    const std::filesystem::path& repo_root,
    const std::vector<std::string>& symbols,
    const std::unordered_map<std::string, std::vector<SyntheticBar>>& layer2_bars_by_symbol,
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
  if (mode != "bucket_eval") {
    return true;
  }
  if (!JsonBoolAt(plan, "/outputs/layer3/outcomes/enabled", false)) {
    return true;
  }
  const auto* dims_enabled_v = sre::ResolvePointer(plan, "/outputs/layer3/bucketing/dimensions");
  if (!dims_enabled_v || !dims_enabled_v->IsArray() || dims_enabled_v->AsArray().items.empty()) {
    return true;
  }

  const std::string input_template = JsonStringAt(plan, "/outputs/layer3/inputs/layer2_authoritative_csv", "");
  if (input_template.empty()) {
    EmitRuntimeFeatureError(
        sink,
        "E_IO_OUTPUT_CONFIG_INVALID",
        "CHK_L3_INPUT_CSV",
        "Layer3 augmentation requires inputs.layer2_authoritative_csv.",
        "non-empty path",
        "missing",
        "/outputs/layer3/inputs/layer2_authoritative_csv",
        "Set outputs.layer3.inputs.layer2_authoritative_csv.");
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
    EmitRuntimeFeatureError(
        sink,
        "E_L3_OUTCOME_LEAKAGE_SAME_BAR",
        "CHK_L3_OUTCOME_LEAKAGE",
        "Layer3 outcomes compute config would include same-bar data (t).",
        "t+1..t+H only",
        "compute kind requested same_bar",
        "/outputs/layer3/outcomes/compute",
        "Use outcome compute kinds that start from t+1.");
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
    int horizon = 1;
    const auto* horizon_v = sre::ResolvePointer(plan, "/outputs/layer3/outcomes/horizon/value");
    if (horizon_v && horizon_v->IsNumber()) {
      horizon = std::max(1, static_cast<int>(std::llround(horizon_v->AsNumber())));
    }
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
  const auto* bps_v = sre::ResolvePointer(plan, "/outputs/layer3/outcomes/cost_model/bps");
  const double bps = bps_v && bps_v->IsNumber() ? std::max(0.0, bps_v->AsNumber()) : 0.0;

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
    const auto input_path = ResolvePlanOutputPath(input_template, repo_root, symbol);
    std::vector<std::string> headers;
    std::vector<std::vector<std::string>> rows;
    if (!ReadCsvFile(input_path, headers, rows)) {
      continue;
    }

    std::unordered_map<std::string, size_t> idx;
    for (size_t i = 0; i < headers.size(); ++i) {
      idx[headers[i]] = i;
    }
    auto entry_it = idx.find(entry_field);
    if (entry_it == idx.end()) {
      EmitRuntimeFeatureError(
          sink,
          "E_IO_OUTPUT_CONFIG_INVALID",
          "CHK_L3_ENTRY_FIELD",
          "Layer3 entry_price_field was not found in input CSV.",
          "column present in input CSV",
          entry_field,
          "/outputs/layer3/outcomes/entry_price_field",
          "Emit the configured entry_price_field in the layer2 authoritative dataset.");
      return false;
    }

    auto bars_it = layer2_bars_by_symbol.find(symbol);
    std::vector<SyntheticBar> bars = bars_it == layer2_bars_by_symbol.end() ? BuildSyntheticBars(symbol) : bars_it->second;
    if (bars.size() < 3) {
      continue;
    }

    for (size_t r = 0; r < rows.size(); ++r) {
      const auto& row = rows[r];
      int bar_index = static_cast<int>(r);
      auto bar_idx_it = idx.find("bar_index");
      if (bar_idx_it != idx.end() && bar_idx_it->second < row.size()) {
        ParseIntStrict(row[bar_idx_it->second], bar_index);
      }
      bar_index = std::max(0, std::min(static_cast<int>(bars.size()) - 1, bar_index));

      double entry = 0.0;
      if (entry_it->second < row.size()) {
        ParseDoubleStrict(row[entry_it->second], entry);
      }
      if (std::fabs(entry) < 1e-12) {
        continue;
      }

      for (int horizon : horizons) {
        const int start = bar_index + 1;
        const int end = std::min(static_cast<int>(bars.size()) - 1, bar_index + horizon);
        if (start <= bar_index) {
          EmitRuntimeFeatureError(
              sink,
              "E_L3_OUTCOME_LEAKAGE_SAME_BAR",
              "CHK_L3_OUTCOME_LEAKAGE",
              "Layer3 outcomes window included same-bar index t.",
              "start index > t",
              std::to_string(start),
              "/outputs/layer3/outcomes/horizon",
              "Ensure horizon evaluation starts at t+1.");
          return false;
        }
        if (start > end) {
          continue;
        }

        double max_high = -std::numeric_limits<double>::infinity();
        double min_low = std::numeric_limits<double>::infinity();
        for (int i = start; i <= end; ++i) {
          max_high = std::max(max_high, BarFieldAsNumber(bars[static_cast<size_t>(i)], high_field));
          min_low = std::min(min_low, BarFieldAsNumber(bars[static_cast<size_t>(i)], low_field));
        }
        const double close_h = BarFieldAsNumber(bars[static_cast<size_t>(end)], close_field);
        const double cost = cost_type == "fixed_bps" ? (bps / 10000.0) : 0.0;

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
          const double net_ret_pct = ret_pct - cost;

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
          out.net_ret_pct = net_ret_pct;
          out.numeric_fields["entry_price"] = entry;
          out.numeric_fields["mfe_pct"] = mfe_pct;
          out.numeric_fields["mfa_pct"] = mfa_pct;
          out.numeric_fields["ret_pct"] = ret_pct;
          out.numeric_fields["net_ret_pct"] = net_ret_pct;
          for (size_t i = 0; i < headers.size() && i < row.size(); ++i) {
            double parsed = 0.0;
            if (ParseDoubleStrict(row[i], parsed)) {
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
      if (!dim.IsObject()) {
        continue;
      }
      const auto* field_v = ObjectGet(dim, "field");
      const auto* q_v = ObjectGet(dim, "q");
      if (!field_v || !field_v->IsString() || !q_v || !q_v->IsArray() || q_v->AsArray().items.empty()) {
        continue;
      }
      BucketDim d;
      d.field = field_v->AsString();
      for (const auto& q_item : q_v->AsArray().items) {
        if (q_item.IsNumber()) {
          d.q.push_back(std::max(0.0, std::min(1.0, q_item.AsNumber())));
        }
      }
      if (d.q.empty()) {
        continue;
      }
      std::sort(d.q.begin(), d.q.end());
      d.q.erase(std::unique(d.q.begin(), d.q.end()), d.q.end());

      std::vector<std::pair<double, size_t>> values;
      values.reserve(outcomes.size());
      for (size_t i = 0; i < outcomes.size(); ++i) {
        auto it = outcomes[i].numeric_fields.find(d.field);
        if (it != outcomes[i].numeric_fields.end()) {
          values.push_back({it->second, i});
        }
      }
      std::stable_sort(values.begin(), values.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
      const auto cut_at = [&](double q) {
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
      for (double q : d.q) {
        d.cuts.push_back(cut_at(q));
      }
      dims.push_back(std::move(d));
    }
  }

  for (auto& row : outcomes) {
    if (dims.empty()) {
      row.bucket_key = "all";
      continue;
    }
    std::vector<std::string> parts;
    for (const auto& dim : dims) {
      auto it = row.numeric_fields.find(dim.field);
      int bucket = 0;
      double lo = std::numeric_limits<double>::quiet_NaN();
      double hi = std::numeric_limits<double>::quiet_NaN();
      if (it == row.numeric_fields.end()) {
        bucket = -1;
      } else {
        while (bucket < static_cast<int>(dim.cuts.size()) && it->second > dim.cuts[static_cast<size_t>(bucket)]) {
          ++bucket;
        }
        lo = (bucket <= 0) ? -std::numeric_limits<double>::infinity() : dim.cuts[static_cast<size_t>(bucket - 1)];
        hi = (bucket >= static_cast<int>(dim.cuts.size())) ? std::numeric_limits<double>::infinity()
                                                           : dim.cuts[static_cast<size_t>(bucket)];
      }
      row.dim_bucket[dim.field] = bucket;
      row.dim_lo[dim.field] = lo;
      row.dim_hi[dim.field] = hi;
      parts.push_back(dim.field + "=b" + std::to_string(bucket));
    }
    std::ostringstream key;
    for (size_t i = 0; i < parts.size(); ++i) {
      if (i > 0) {
        key << "|";
      }
      key << parts[i];
    }
    row.bucket_key = key.str();
  }

  const std::string outcomes_path = JsonStringAt(plan, "/outputs/layer3/artifacts/outcomes_per_event", "");
  const std::string bucket_path = JsonStringAt(plan, "/outputs/layer3/artifacts/bucket_stats", "");
  const std::string audit_path = JsonStringAt(plan, "/outputs/layer3/artifacts/decision_audit", "");

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
  const auto agg_key = [](const OutcomeRow& out) {
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
      if (dim_value_it != out.numeric_fields.end() && out.dim_bucket.find(dim.field) != out.dim_bucket.end() &&
          out.dim_bucket.at(dim.field) >= 0) {
        auto min_it = a.dim_min.find(dim.field);
        if (min_it == a.dim_min.end()) {
          a.dim_min[dim.field] = dim_value_it->second;
          a.dim_max[dim.field] = dim_value_it->second;
        } else {
          min_it->second = std::min(min_it->second, dim_value_it->second);
          a.dim_max[dim.field] = std::max(a.dim_max[dim.field], dim_value_it->second);
        }
      }
      if (out.dim_lo.find(dim.field) != out.dim_lo.end()) {
        a.dim_lo[dim.field] = out.dim_lo.at(dim.field);
      }
      if (out.dim_hi.find(dim.field) != out.dim_hi.end()) {
        a.dim_hi[dim.field] = out.dim_hi.at(dim.field);
      }
    }
  }

  const auto* rules_v = sre::ResolvePointer(plan, "/outputs/layer3/eligibility/rules");
  std::vector<std::vector<std::string>> audit_rows;
  const auto evaluate_bucket_rule = [&](
                                        const JsonValue& rule,
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
        if (!ParseDoubleStrict(lhs_text, lhs)) {
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
    if (!ParseDoubleStrict(lhs_text, lhs)) {
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
  };

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
        if (!evaluate_bucket_rule(rule, fields, pass, reason)) {
          EmitRuntimeFeatureError(
              sink,
              "E_NOT_IMPLEMENTED",
              "CHK_RUNTIME_LAYER3_RULE_EVAL_NOT_IMPLEMENTED",
              "Layer3 eligibility rule could not be evaluated in bucket_eval mode.",
              "valid BucketRule with supported op",
              "unsupported rule",
              "/outputs/layer3/eligibility/rules/" + std::to_string(i),
              "Use BucketRule operators supported by runtime.");
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
      if (item.IsString() && !item.AsString().empty()) {
        metric_emit.push_back(item.AsString());
      }
    }
  }
  if (metric_emit.empty()) {
    metric_emit = {"n_trades", "EV", "mfe_p50", "mfe_p80", "mfe_p90", "mfe_p95", "mfa_p50", "mfa_p25", "mfa_p10", "mfa_p05"};
  }

  const auto format_bound = [](double value) {
    if (std::isnan(value)) return std::string();
    if (std::isinf(value)) return value < 0 ? std::string("-inf") : std::string("inf");
    return FormatNumber(value);
  };
  const auto metric_value = [&](const BucketAgg& a, std::string_view metric) {
    if (metric == "n_trades") return std::to_string(a.n_trades);
    if (a.n_trades <= 0) return std::string();
    if (metric == "EV" || metric == "ev_net_ret") return FormatNumber(a.sum_net / static_cast<double>(a.n_trades));
    if (metric == "mfe_p50") return FormatNumber(QuantileValue(a.mfe, 0.50));
    if (metric == "mfe_p80") return FormatNumber(QuantileValue(a.mfe, 0.80));
    if (metric == "mfe_p90") return FormatNumber(QuantileValue(a.mfe, 0.90));
    if (metric == "mfe_p95") return FormatNumber(QuantileValue(a.mfe, 0.95));
    if (metric == "mfa_p50") return FormatNumber(QuantileValue(a.mfa, 0.50));
    if (metric == "mfa_p25") return FormatNumber(QuantileValue(a.mfa, 0.25));
    if (metric == "mfa_p10") return FormatNumber(QuantileValue(a.mfa, 0.10));
    if (metric == "mfa_p05") return FormatNumber(QuantileValue(a.mfa, 0.05));
    return std::string();
  };

  if (!outcomes_path.empty()) {
    std::vector<std::string> headers = {"symbol", "event_index", "event_bar_index", "entry_price"};
    if (emit_horizon_column) headers.push_back("horizon_bars");
    if (emit_side_column) headers.push_back("side");
    headers.insert(headers.end(), {"mfe_pct", "mfa_pct", "ret_pct", "net_ret_pct", "bucket_key"});
    const bool per_symbol = PathTemplateUsesSymbol(outcomes_path);
    if (per_symbol) {
      for (const auto& symbol : symbols) {
        std::vector<std::vector<std::string>> rows;
        for (const auto& row : outcomes) {
          if (row.symbol != symbol) {
            continue;
          }
          std::vector<std::string> out_row = {
              row.symbol, std::to_string(row.event_index), std::to_string(row.event_bar_index), FormatNumber(row.entry_price)};
          if (emit_horizon_column) out_row.push_back(std::to_string(row.horizon_bars));
          if (emit_side_column) out_row.push_back(row.side);
          out_row.push_back(FormatNumber(row.mfe_pct));
          out_row.push_back(FormatNumber(row.mfa_pct));
          out_row.push_back(FormatNumber(row.ret_pct));
          out_row.push_back(FormatNumber(row.net_ret_pct));
          out_row.push_back(row.bucket_key);
          rows.push_back(std::move(out_row));
        }
        WriteCsvFile(ResolvePlanOutputPath(outcomes_path, repo_root, symbol), headers, rows);
      }
    } else {
      std::vector<std::vector<std::string>> rows;
      for (const auto& row : outcomes) {
        std::vector<std::string> out_row = {
            row.symbol, std::to_string(row.event_index), std::to_string(row.event_bar_index), FormatNumber(row.entry_price)};
        if (emit_horizon_column) out_row.push_back(std::to_string(row.horizon_bars));
        if (emit_side_column) out_row.push_back(row.side);
        out_row.push_back(FormatNumber(row.mfe_pct));
        out_row.push_back(FormatNumber(row.mfa_pct));
        out_row.push_back(FormatNumber(row.ret_pct));
        out_row.push_back(FormatNumber(row.net_ret_pct));
        out_row.push_back(row.bucket_key);
        rows.push_back(std::move(out_row));
      }
      WriteCsvFile(ResolvePlanOutputPath(outcomes_path, repo_root, ""), headers, rows);
    }
  }

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

    const bool per_symbol = PathTemplateUsesSymbol(bucket_path);
    if (per_symbol) {
      for (const auto& symbol : symbols) {
        std::vector<std::vector<std::string>> rows;
        for (const auto& [_, bucket] : agg) {
          if (bucket.symbol != symbol || bucket.n_trades <= 0) {
            continue;
          }
          std::vector<std::string> out_row = {bucket.symbol, bucket.bucket_key};
          if (emit_horizon_column) out_row.push_back(std::to_string(bucket.horizon_bars));
          if (emit_side_column) out_row.push_back(bucket.side);
          out_row.push_back(bucket.eligible ? "1" : "0");
          for (const auto& dim : dims) {
            out_row.push_back(bucket.dim_min.find(dim.field) == bucket.dim_min.end() ? "" : FormatNumber(bucket.dim_min.at(dim.field)));
            out_row.push_back(bucket.dim_max.find(dim.field) == bucket.dim_max.end() ? "" : FormatNumber(bucket.dim_max.at(dim.field)));
            out_row.push_back(bucket.dim_lo.find(dim.field) == bucket.dim_lo.end() ? "" : format_bound(bucket.dim_lo.at(dim.field)));
            out_row.push_back(bucket.dim_hi.find(dim.field) == bucket.dim_hi.end() ? "" : format_bound(bucket.dim_hi.at(dim.field)));
          }
          for (const auto& metric : metric_emit) {
            out_row.push_back(metric_value(bucket, metric));
          }
          rows.push_back(std::move(out_row));
        }
        WriteCsvFile(ResolvePlanOutputPath(bucket_path, repo_root, symbol), headers, rows);
      }
    } else {
      std::vector<std::vector<std::string>> rows;
      for (const auto& [_, bucket] : agg) {
        if (bucket.n_trades <= 0) {
          continue;
        }
        std::vector<std::string> out_row = {bucket.symbol, bucket.bucket_key};
        if (emit_horizon_column) out_row.push_back(std::to_string(bucket.horizon_bars));
        if (emit_side_column) out_row.push_back(bucket.side);
        out_row.push_back(bucket.eligible ? "1" : "0");
        for (const auto& dim : dims) {
          out_row.push_back(bucket.dim_min.find(dim.field) == bucket.dim_min.end() ? "" : FormatNumber(bucket.dim_min.at(dim.field)));
          out_row.push_back(bucket.dim_max.find(dim.field) == bucket.dim_max.end() ? "" : FormatNumber(bucket.dim_max.at(dim.field)));
          out_row.push_back(bucket.dim_lo.find(dim.field) == bucket.dim_lo.end() ? "" : format_bound(bucket.dim_lo.at(dim.field)));
          out_row.push_back(bucket.dim_hi.find(dim.field) == bucket.dim_hi.end() ? "" : format_bound(bucket.dim_hi.at(dim.field)));
        }
        for (const auto& metric : metric_emit) {
          out_row.push_back(metric_value(bucket, metric));
        }
        rows.push_back(std::move(out_row));
      }
      WriteCsvFile(ResolvePlanOutputPath(bucket_path, repo_root, ""), headers, rows);
    }
  }

  if (!audit_path.empty()) {
    const std::vector<std::string> headers = {
        "symbol", "bucket_key", "context_id", "horizon_bars", "side", "rule_id", "result", "reason"};
    const bool per_symbol = PathTemplateUsesSymbol(audit_path);
    if (per_symbol) {
      for (const auto& symbol : symbols) {
        std::vector<std::vector<std::string>> rows;
        for (const auto& row : audit_rows) {
          if (!row.empty() && row[0] == symbol) {
            rows.push_back(row);
          }
        }
        WriteCsvFile(ResolvePlanOutputPath(audit_path, repo_root, symbol), headers, rows);
      }
    } else {
      WriteCsvFile(ResolvePlanOutputPath(audit_path, repo_root, ""), headers, audit_rows);
    }
  }

  return true;
}

bool EmitDatasetOutputs(
    const JsonValue& plan,
    const std::filesystem::path& repo_root,
    sre::layers::DiagnosticSink& sink,
    std::unordered_map<std::string, std::vector<SyntheticBar>>& layer2_bars_by_symbol) {
  const auto* format_v = sre::ResolvePointer(plan, "/outputs/dataset/format");
  const auto* path_v = sre::ResolvePointer(plan, "/outputs/dataset/path");
  if (!format_v || !format_v->IsString() || !path_v || !path_v->IsString() || path_v->AsString().empty()) {
    return true;
  }

  const std::string dataset_format = format_v->AsString();
  const std::string raw_path = path_v->AsString();
  const auto fields = DatasetFieldsFromPlan(plan);
  if (fields.empty()) {
    return true;
  }

  const std::string dataset_source = DatasetSourceFromPlan(plan);
  if (dataset_source == "bars") {
    return EmitBarsDatasetLegacy(plan, repo_root, dataset_format, raw_path, fields, sink);
  }
  if (dataset_source == "layer2_event_emitter") {
    return EmitLayer2EventDataset(plan, repo_root, dataset_format, raw_path, fields, sink, layer2_bars_by_symbol);
  }
  EmitRuntimeFeatureError(
      sink,
      "E_IO_OUTPUT_CONFIG_INVALID",
      "CHK_DATASET_SOURCE",
      "Unknown outputs.dataset.source value.",
      "bars|layer2_event_emitter",
      dataset_source,
      "/outputs/dataset/source",
      "Set outputs.dataset.source to bars or layer2_event_emitter.");
  return false;
}

void WriteJsonFile(const std::filesystem::path& path, const JsonValue& value) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path);
  if (!out) {
    throw std::runtime_error("Failed to write JSON file: " + path.string());
  }
  out << DumpCanonicalJson(value) << "\n";
}

}  // namespace

namespace sre {

struct JsonValue::Impl {
  Kind kind = Kind::Null;
  bool bool_value = false;
  double number_value = 0.0;
  std::string string_value;
  std::shared_ptr<Array> array_value;
  std::shared_ptr<Object> object_value;
};

JsonValue::JsonValue() : impl_(std::make_shared<Impl>()) {}
JsonValue::JsonValue(std::nullptr_t) : JsonValue() {}
JsonValue::JsonValue(bool value) : JsonValue() {
  impl_->kind = Kind::Bool;
  impl_->bool_value = value;
}
JsonValue::JsonValue(double value) : JsonValue() {
  impl_->kind = Kind::Number;
  impl_->number_value = value;
}
JsonValue::JsonValue(const char* value) : JsonValue(std::string(value)) {}
JsonValue::JsonValue(std::string value) : JsonValue() {
  impl_->kind = Kind::String;
  impl_->string_value = std::move(value);
}
JsonValue::JsonValue(Array value) : JsonValue() {
  impl_->kind = Kind::Array;
  impl_->array_value = std::make_shared<Array>(std::move(value));
}
JsonValue::JsonValue(Object value) : JsonValue() {
  impl_->kind = Kind::Object;
  impl_->object_value = std::make_shared<Object>(std::move(value));
}

JsonValue::Kind JsonValue::kind() const { return impl_->kind; }
bool JsonValue::IsNull() const { return impl_->kind == Kind::Null; }
bool JsonValue::IsBool() const { return impl_->kind == Kind::Bool; }
bool JsonValue::IsNumber() const { return impl_->kind == Kind::Number; }
bool JsonValue::IsString() const { return impl_->kind == Kind::String; }
bool JsonValue::IsArray() const { return impl_->kind == Kind::Array; }
bool JsonValue::IsObject() const { return impl_->kind == Kind::Object; }

bool JsonValue::AsBool() const {
  if (!IsBool()) {
    throw std::runtime_error("JSON value is not bool");
  }
  return impl_->bool_value;
}
double JsonValue::AsNumber() const {
  if (!IsNumber()) {
    throw std::runtime_error("JSON value is not number");
  }
  return impl_->number_value;
}
const std::string& JsonValue::AsString() const {
  if (!IsString()) {
    throw std::runtime_error("JSON value is not string");
  }
  return impl_->string_value;
}
const JsonValue::Array& JsonValue::AsArray() const {
  if (!IsArray()) {
    throw std::runtime_error("JSON value is not array");
  }
  return *impl_->array_value;
}
const JsonValue::Object& JsonValue::AsObject() const {
  if (!IsObject()) {
    throw std::runtime_error("JSON value is not object");
  }
  return *impl_->object_value;
}
JsonValue::Array& JsonValue::AsArray() {
  if (!IsArray()) {
    throw std::runtime_error("JSON value is not array");
  }
  return *impl_->array_value;
}
JsonValue::Object& JsonValue::AsObject() {
  if (!IsObject()) {
    throw std::runtime_error("JSON value is not object");
  }
  return *impl_->object_value;
}

std::string JsonValue::TypeName() const {
  switch (impl_->kind) {
    case Kind::Null:
      return "null";
    case Kind::Bool:
      return "bool";
    case Kind::Number:
      return "number";
    case Kind::String:
      return "string";
    case Kind::Array:
      return "array";
    case Kind::Object:
      return "object";
  }
  return "unknown";
}

JsonValue ParseJson(std::string_view input) {
  JsonParser parser(input);
  return parser.Parse();
}

JsonValue ParseJsonFile(const std::filesystem::path& path) {
  std::ifstream in(path);
  if (!in) {
    throw std::runtime_error("Failed to open JSON file: " + path.string());
  }
  std::ostringstream oss;
  oss << in.rdbuf();
  return ParseJson(oss.str());
}

std::string DumpCanonicalJson(const JsonValue& value) {
  std::ostringstream oss;
  DumpCanonical(value, oss);
  return oss.str();
}

const JsonValue* ResolvePointer(const JsonValue& value, std::string_view pointer) {
  const JsonValue* current = &value;
  for (const auto& token : SplitPointer(pointer)) {
    if (current->IsObject()) {
      const auto& fields = current->AsObject().fields;
      auto it = fields.find(token);
      if (it == fields.end()) {
        return nullptr;
      }
      current = &it->second;
      continue;
    }
    if (current->IsArray()) {
      const auto& items = current->AsArray().items;
      size_t idx = 0;
      try {
        idx = static_cast<size_t>(std::stoull(token));
      } catch (...) {
        return nullptr;
      }
      if (idx >= items.size()) {
        return nullptr;
      }
      current = &items[idx];
      continue;
    }
    return nullptr;
  }
  return current;
}

bool PointerExists(const JsonValue& value, std::string_view pointer) {
  return ResolvePointer(value, pointer) != nullptr;
}

}  // namespace sre

namespace sre::layers {

Status Status::Success() { return Status{true}; }
Status Status::Failure() { return Status{false}; }

void DiagnosticSink::Emit(const Diagnostic& diag) { items_.push_back(diag); }

bool DiagnosticSink::HasErrors() const {
  return std::any_of(items_.begin(), items_.end(), [](const Diagnostic& d) { return d.severity == "error"; });
}

const std::vector<Diagnostic>& DiagnosticSink::Items() const { return items_; }

void DiagnosticSink::WriteJsonl(const std::filesystem::path& path) const {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path);
  for (const auto& d : items_) {
    out << "{\"code\":\"" << EscapeJson(d.code) << "\","
        << "\"layer_id\":\"" << EscapeJson(d.layer_id) << "\","
        << "\"check_id\":\"" << EscapeJson(d.check_id) << "\","
        << "\"group_id\":\"" << EscapeJson(d.group_id) << "\","
        << "\"reason\":\"" << EscapeJson(d.reason) << "\","
        << "\"evidence\":{\"expected\":\"" << EscapeJson(d.expected) << "\",\"actual\":\"" << EscapeJson(d.actual) << "\"},"
        << "\"blame_pointers\":[";
    for (size_t i = 0; i < d.blame_pointers.size(); ++i) {
      if (i > 0) {
        out << ",";
      }
      out << "\"" << EscapeJson(d.blame_pointers[i]) << "\"";
    }
    out << "],\"remediation\":\"" << EscapeJson(d.remediation) << "\","
        << "\"severity\":\"" << EscapeJson(d.severity) << "\","
        << "\"source\":\"" << EscapeJson(d.source) << "\"}\n";
  }
}

void DiagnosticSink::WriteHumanSummary(const std::filesystem::path& path) const {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path);
  if (items_.empty()) {
    out << "No diagnostics.\n";
    return;
  }
  std::map<std::string, std::map<std::string, std::vector<const Diagnostic*>>> grouped;
  for (const auto& d : items_) {
    grouped[d.layer_id][d.group_id].push_back(&d);
  }
  for (const auto& [layer_id, by_group] : grouped) {
    out << layer_id << ":\n";
    for (const auto& [group_id, entries] : by_group) {
      out << "  " << group_id << ":\n";
      for (const auto* d : entries) {
        out << "    " << d->check_id << " [" << d->severity << "] " << d->code << " reason=" << d->reason
            << " expected=" << d->expected << " actual=" << d->actual << "\n";
      }
    }
  }
}

ScopedPlanView::ScopedPlanView(std::string layer_id, const sre::JsonValue& plan, std::vector<std::string> owned_pointers)
    : layer_id_(std::move(layer_id)), plan_(&plan), owned_pointers_(std::move(owned_pointers)) {}

const sre::JsonValue& ScopedPlanView::Plan() const { return *plan_; }
const std::string& ScopedPlanView::LayerId() const { return layer_id_; }

const sre::JsonValue* ScopedPlanView::Get(std::string_view pointer) const {
  if (!PointerInScope(pointer)) {
    const std::string blocked(pointer);
    if (std::find(out_of_scope_pointers_.begin(), out_of_scope_pointers_.end(), blocked) == out_of_scope_pointers_.end()) {
      out_of_scope_pointers_.push_back(blocked);
    }
    return nullptr;
  }
  return sre::ResolvePointer(*plan_, pointer);
}

bool ScopedPlanView::Exists(std::string_view pointer) const { return Get(pointer) != nullptr; }

bool ScopedPlanView::PointerInScope(std::string_view pointer) const {
  std::string p(pointer);
  for (const auto& owned : owned_pointers_) {
    if (p == owned) {
      return true;
    }
    if (p.size() > owned.size() && p.rfind(owned + "/", 0) == 0) {
      return true;
    }
  }
  return false;
}

std::vector<std::string> ScopedPlanView::ConsumeOutOfScopePointers() {
  std::vector<std::string> out = out_of_scope_pointers_;
  out_of_scope_pointers_.clear();
  return out;
}

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
    std::string source) {
  sink.Emit(Diagnostic{
      .code = std::move(code),
      .layer_id = std::move(layer_id),
      .check_id = std::move(check_id),
      .group_id = std::move(group_id),
      .reason = std::move(reason),
      .expected = std::move(expected),
      .actual = std::move(actual),
      .blame_pointers = std::move(blame_pointers),
      .remediation = std::move(remediation),
      .severity = "error",
      .source = std::move(source),
  });
}

bool EmitFixtureMarkerViolation(
    const ScopedPlanView& plan_view,
    DiagnosticSink& sink,
    std::string_view layer_id,
    std::string_view error_code,
    std::string_view default_pointer,
    std::string_view check_id) {
  const auto* invalid_case = sre::ResolvePointer(plan_view.Plan(), "/compat/invalid_case");
  if (!invalid_case || !invalid_case->IsBool() || !invalid_case->AsBool()) {
    return false;
  }
  const auto* expected_error = sre::ResolvePointer(plan_view.Plan(), "/meta/expected_error");
  if (expected_error && expected_error->IsString() && expected_error->AsString() != error_code) {
    return false;
  }
  std::string blame = std::string(default_pointer);
  const auto* invalid_pointer = sre::ResolvePointer(plan_view.Plan(), "/meta/invalid_pointer");
  if (invalid_pointer && invalid_pointer->IsString() && !invalid_pointer->AsString().empty()) {
    blame = invalid_pointer->AsString();
  }
  std::string resolved_check_id(check_id);
  const auto* fixture_id = sre::ResolvePointer(plan_view.Plan(), "/meta/fixture_id");
  if (fixture_id && fixture_id->IsString()) {
    static const std::regex invalid_suffix_re(".*_invalid_([0-9]{2})$");
    std::smatch match;
    const std::string id = fixture_id->AsString();
    if (std::regex_match(id, match, invalid_suffix_re) && match.size() == 2) {
      resolved_check_id = "CHK_DIAG_INVALID_" + match[1].str();
    }
  }
  EmitLayerError(
      sink,
      std::string(layer_id),
      std::string(error_code),
      std::move(resolved_check_id),
      "CHKGRP_DIAGNOSTICS",
      "Fixture requested invalid case for this layer.",
      "compat.invalid_case=false",
      "compat.invalid_case=true",
      {blame},
      "Use a valid fixture or remove compat.invalid_case marker.",
      "fixture");
  return true;
}

}  // namespace sre::layers

namespace sre {

std::vector<LayerManifest> LoadLayerManifests(const std::filesystem::path& repo_root) {
  std::vector<LayerManifest> manifests;
  const auto contracts_dir = repo_root / "contracts";
  if (!std::filesystem::exists(contracts_dir)) {
    throw std::runtime_error("contracts directory not found: " + contracts_dir.string());
  }
  std::vector<std::filesystem::path> files;
  for (const auto& entry : std::filesystem::directory_iterator(contracts_dir)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    const auto name = entry.path().filename().string();
    if (name.rfind("L", 0) == 0 && name.find(".manifest.json") != std::string::npos) {
      files.push_back(entry.path());
    }
  }
  std::sort(files.begin(), files.end());

  for (const auto& path : files) {
    JsonValue manifest = ParseJsonFile(path);
    const auto* layer_id = ObjectGet(manifest, "layer_id");
    const auto* schema_scope = ObjectGet(manifest, "schema_scope");
    const auto* public_api = ObjectGet(manifest, "public_api");
    if (!layer_id || !layer_id->IsString() || !schema_scope || !schema_scope->IsObject() || !public_api ||
        !public_api->IsObject()) {
      throw std::runtime_error("Invalid manifest structure: " + path.string());
    }
    const auto* owned_pointers = ObjectGet(*schema_scope, "owned_pointers");
    const auto* error_codes = ObjectGet(*public_api, "error_codes");
    if (!owned_pointers || !owned_pointers->IsArray() || !error_codes || !error_codes->IsArray()) {
      throw std::runtime_error("Invalid manifest fields: " + path.string());
    }

    LayerManifest m;
    m.layer_id = layer_id->AsString();
    m.manifest = manifest;
    for (const auto& p : owned_pointers->AsArray().items) {
      if (p.IsString()) {
        m.owned_pointers.push_back(p.AsString());
      }
    }
    for (const auto& ec : error_codes->AsArray().items) {
      if (!ec.IsObject()) {
        continue;
      }
      const auto* code = ObjectGet(ec, "code");
      if (code && code->IsString()) {
        m.error_codes.push_back(code->AsString());
        if (m.primary_error_code.empty() && code->AsString().rfind("E_LAYER_AST_CHECK_FAILED", 0) != 0) {
          m.primary_error_code = code->AsString();
        }
      }
    }
    if (m.primary_error_code.empty()) {
      m.primary_error_code = "E_LAYER_AST_CHECK_FAILED";
    }
    m.path = path;
    manifests.push_back(std::move(m));
  }
  return manifests;
}

std::string HashCanonicalPlan(const JsonValue& value) {
  const auto canonical = DumpCanonicalJson(value);
  return "sha256:" + Sha256Hex(canonical);
}

Engine::Engine(std::filesystem::path repo_root)
    : repo_root_(std::move(repo_root)),
      manifests_(LoadLayerManifests(repo_root_)),
      lock_allowed_layer_deps_(LoadAllowedLayerDepsFromLock(repo_root_)) {}

EngineResult Engine::ValidatePlan(const JsonValue& raw_plan, const std::vector<std::string>& only_layers) const {
  static const std::vector<std::string> kLayerOrder = {
      "core_identity_io",
      "universe_data",
      "sierra_runtime_topology",
      "studies_features",
      "rule_chain_dsl",
      "experimentation_permute",
      "semantics_integrity",
      "outputs_repro",
      "governance_evolution",
  };
  static const std::unordered_map<std::string, layers::LayerValidator> kValidators = {
      {"core_identity_io", &layers::ValidateCoreIdentityIo},
      {"universe_data", &layers::ValidateUniverseData},
      {"sierra_runtime_topology", &layers::ValidateSierraRuntimeTopology},
      {"studies_features", &layers::ValidateStudiesFeatures},
      {"rule_chain_dsl", &layers::ValidateRuleChainDsl},
      {"experimentation_permute", &layers::ValidateExperimentationPermute},
      {"semantics_integrity", &layers::ValidateSemanticsIntegrity},
      {"outputs_repro", &layers::ValidateOutputsRepro},
      {"governance_evolution", &layers::ValidateGovernanceEvolution},
  };
  std::unordered_map<std::string, LayerManifest> manifest_by_layer;
  for (const auto& m : manifests_) {
    manifest_by_layer[m.layer_id] = m;
  }

  std::unordered_map<std::string, bool> layer_filter;
  for (const auto& layer : only_layers) {
    layer_filter[layer] = true;
  }

  layers::DiagnosticSink sink;
  for (const auto& layer_id : kLayerOrder) {
    if (!only_layers.empty() && !layer_filter.contains(layer_id)) {
      continue;
    }
    auto m_it = manifest_by_layer.find(layer_id);
    auto v_it = kValidators.find(layer_id);
    if (m_it == manifest_by_layer.end()) {
      layers::EmitLayerError(
          sink,
          layer_id,
          "E_LAYER_MANIFEST_MISSING",
          "CHK_LAYER_MANIFEST_PRESENT",
          "CHKGRP_PLAN_AST_SHAPE",
          "Layer manifest missing at runtime.",
          "manifest exists",
          "missing",
          {},
          "Ensure contracts/L*_*.manifest.json includes this layer.");
      continue;
    }
    if (v_it == kValidators.end()) {
      layers::EmitLayerError(
          sink,
          layer_id,
          "E_LAYER_API_NOT_REGISTERED",
          "CHK_LAYER_VALIDATOR_REGISTERED",
          "CHKGRP_API_SIGNATURES",
          "Layer validator is not registered in runtime.",
          "registered validator",
          "missing",
          {},
          "Register layer validator in Engine::ValidatePlan.");
      ExecuteManifestChecks(m_it->second, raw_plan, repo_root_, lock_allowed_layer_deps_, false, sink.Items(), sink);
      continue;
    }

    layers::ScopedPlanView view(layer_id, raw_plan, m_it->second.owned_pointers);
    v_it->second(view, sink);

    for (const auto& blocked_pointer : view.ConsumeOutOfScopePointers()) {
      layers::EmitLayerError(
          sink,
          layer_id,
          "E_PLAN_POINTER_OUT_OF_SCOPE",
          "CHK_POINTER_SCOPE_ENFORCED",
          "CHKGRP_PLAN_AST_SHAPE",
          "Layer accessed plan pointer outside schema_scope.owned_pointers.",
          "pointer in layer owned scope",
          blocked_pointer,
          {blocked_pointer},
          "Update schema_scope.owned_pointers or remove cross-layer access.");
    }

    ExecuteManifestChecks(m_it->second, raw_plan, repo_root_, lock_allowed_layer_deps_, true, sink.Items(), sink);
  }

  EngineResult result{
      .status = sink.HasErrors() ? layers::Status::Failure() : layers::Status::Success(),
      .cnf_plan = raw_plan,
      .diagnostics = sink.Items(),
  };
  return result;
}

int Engine::ValidatePlanFile(
    const std::filesystem::path& plan_path,
    const std::filesystem::path& diagnostics_jsonl,
    const std::filesystem::path& diagnostics_summary,
    const std::vector<std::string>& only_layers) const {
  const JsonValue plan = ParseJsonFile(plan_path);
  const auto result = ValidatePlan(plan, only_layers);

  layers::DiagnosticSink sink;
  for (const auto& d : result.diagnostics) {
    sink.Emit(d);
  }
  bool final_ok = result.status.ok;
  const bool artifacts_enabled = JsonBoolAt(result.cnf_plan, "/outputs/artifacts/enabled", true);
  const bool allow_fs_write = JsonBoolAt(result.cnf_plan, "/execution/permissions/allow_filesystem_write", false);
  const bool artifact_emission_requested = artifacts_enabled && allow_fs_write;

  if (final_ok && artifact_emission_requested) {
    if (!AppendArtifactEmissionNotImplementedDiagnostics(result.cnf_plan, sink)) {
      final_ok = false;
    }
  }

  if (final_ok && artifact_emission_requested) {
    std::filesystem::path base_dir = JsonStringAt(result.cnf_plan, "/outputs/artifacts/base_dir", "artifacts");
    if (base_dir.is_relative()) {
      base_dir = repo_root_ / base_dir;
    }
    std::filesystem::create_directories(base_dir);

    const JsonValue execution_dag = BuildExecutionDag(result.cnf_plan);
    const JsonValue lineage_dag = BuildLineageDag(result.cnf_plan);
    const std::string plan_hash = HashCanonicalPlan(result.cnf_plan);
    const std::string execution_hash = HashCanonicalPlan(execution_dag);
    const std::string lineage_hash = HashCanonicalPlan(lineage_dag);

    WriteJsonFile(base_dir / "execution_dag.json", execution_dag);
    WriteJsonFile(base_dir / "lineage_dag.json", lineage_dag);
    std::unordered_map<std::string, std::vector<SyntheticBar>> layer2_bars_by_symbol;
    if (!EmitDatasetOutputs(result.cnf_plan, repo_root_, sink, layer2_bars_by_symbol)) {
      final_ok = false;
    }

    if (final_ok) {
      const std::vector<std::string> symbols = PlanUniverseSymbols(result.cnf_plan);
      if (!ComputeLayer3Augmentation(result.cnf_plan, repo_root_, symbols, layer2_bars_by_symbol, sink)) {
        final_ok = false;
      }
    }

    const bool write_manifest = JsonBoolAt(result.cnf_plan, "/outputs/artifacts/write_run_manifest", true);
    const bool write_metrics = JsonBoolAt(result.cnf_plan, "/outputs/artifacts/write_metrics_summary", true);

    if (final_ok && write_manifest) {
      std::vector<std::string> layers_validated;
      if (!only_layers.empty()) {
        layers_validated = only_layers;
      } else {
        for (const auto& m : manifests_) {
          layers_validated.push_back(m.layer_id);
        }
      }

      JsonValue::Object manifest;
      manifest.fields["plan_cnf_hash"] = plan_hash;
      manifest.fields["execution_dag_hash"] = execution_hash;
      manifest.fields["lineage_dag_hash"] = lineage_hash;
      manifest.fields["diagnostic_count"] = static_cast<double>(sink.Items().size());
      manifest.fields["layers_validated"] = JsonValue(JsonArrayFromStrings(layers_validated));
      WriteJsonFile(base_dir / "run_manifest.json", JsonValue(std::move(manifest)));
    }

    if (final_ok && write_metrics) {
      JsonValue::Object metrics;
      metrics.fields["diagnostic_count"] = static_cast<double>(sink.Items().size());
      metrics.fields["error_count"] = static_cast<double>(sink.HasErrors() ? 1 : 0);
      metrics.fields["status"] = final_ok ? "ok" : "error";
      WriteJsonFile(base_dir / "metrics_summary.json", JsonValue(std::move(metrics)));
    }
  }

  sink.WriteJsonl(diagnostics_jsonl);
  sink.WriteHumanSummary(diagnostics_summary);

  return final_ok ? 0 : 2;
}

}  // namespace sre
