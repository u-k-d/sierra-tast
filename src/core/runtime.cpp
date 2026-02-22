#include "sre/runtime.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <regex>
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

ScopedPlanView::ScopedPlanView(const sre::JsonValue& plan, std::vector<std::string> owned_pointers)
    : plan_(&plan), owned_pointers_(std::move(owned_pointers)) {}

const sre::JsonValue& ScopedPlanView::Plan() const { return *plan_; }

const sre::JsonValue* ScopedPlanView::Get(std::string_view pointer) const {
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
  const auto* invalid_case = plan_view.Get("/compat/invalid_case");
  if (!invalid_case || !invalid_case->IsBool() || !invalid_case->AsBool()) {
    return false;
  }
  const auto* expected_error = plan_view.Get("/meta/expected_error");
  if (expected_error && expected_error->IsString() && expected_error->AsString() != error_code) {
    return false;
  }
  std::string blame = std::string(default_pointer);
  const auto* invalid_pointer = plan_view.Get("/meta/invalid_pointer");
  if (invalid_pointer && invalid_pointer->IsString() && !invalid_pointer->AsString().empty()) {
    blame = invalid_pointer->AsString();
  }
  EmitLayerError(
      sink,
      std::string(layer_id),
      std::string(error_code),
      std::string(check_id),
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
      if (code && code->IsString() && code->AsString().rfind("E_LAYER_AST_CHECK_FAILED", 0) != 0) {
        m.primary_error_code = code->AsString();
        break;
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

Engine::Engine(std::filesystem::path repo_root) : repo_root_(std::move(repo_root)), manifests_(LoadLayerManifests(repo_root_)) {}

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
    if (m_it == manifest_by_layer.end() || v_it == kValidators.end()) {
      continue;
    }
    layers::ScopedPlanView view(raw_plan, m_it->second.owned_pointers);
    v_it->second(view, sink);
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
  sink.WriteJsonl(diagnostics_jsonl);
  sink.WriteHumanSummary(diagnostics_summary);

  if (result.status.ok) {
    const bool artifacts_enabled = JsonBoolAt(result.cnf_plan, "/outputs/artifacts/enabled", true);
    const bool allow_fs_write = JsonBoolAt(result.cnf_plan, "/execution/permissions/allow_filesystem_write", false);
    if (artifacts_enabled && allow_fs_write) {
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

      const bool write_manifest = JsonBoolAt(result.cnf_plan, "/outputs/artifacts/write_run_manifest", true);
      const bool write_metrics = JsonBoolAt(result.cnf_plan, "/outputs/artifacts/write_metrics_summary", true);

      if (write_manifest) {
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
        manifest.fields["diagnostic_count"] = static_cast<double>(result.diagnostics.size());
        manifest.fields["layers_validated"] = JsonValue(JsonArrayFromStrings(layers_validated));
        WriteJsonFile(base_dir / "run_manifest.json", JsonValue(std::move(manifest)));
      }

      if (write_metrics) {
        JsonValue::Object metrics;
        metrics.fields["diagnostic_count"] = static_cast<double>(result.diagnostics.size());
        metrics.fields["error_count"] = static_cast<double>(sink.HasErrors() ? 1 : 0);
        metrics.fields["status"] = result.status.ok ? "ok" : "error";
        WriteJsonFile(base_dir / "metrics_summary.json", JsonValue(std::move(metrics)));
      }
    }
  }
  return result.status.ok ? 0 : 2;
}

}  // namespace sre
