// json.hpp — a small, dependency-free JSON parser/serializer.
//
// This is intentionally written from scratch (no third-party libraries) so the
// flow binary can be compiled with nothing but a C++17 toolchain. It supports
// the subset of JSON needed for graph I/O: objects, arrays, strings, numbers
// (integer and double), booleans and null.
#ifndef UFE_JSON_HPP
#define UFE_JSON_HPP

#include <cctype>
#include <cstdint>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace ufe {
namespace json {

enum class Type { Null, Bool, Number, String, Array, Object };

class Value;
using Array = std::vector<Value>;
// Ordered map so serialized output is deterministic (nice for diffs/tests).
using Object = std::map<std::string, Value>;

class Value {
 public:
  Value() : type_(Type::Null) {}
  Value(std::nullptr_t) : type_(Type::Null) {}
  Value(bool b) : type_(Type::Bool), bool_(b) {}
  Value(int n) : type_(Type::Number), num_(n), is_int_(true) {}
  Value(long long n) : type_(Type::Number), num_(double(n)), is_int_(true) {}
  Value(double n) : type_(Type::Number), num_(n), is_int_(false) {}
  Value(const char* s) : type_(Type::String), str_(s) {}
  Value(const std::string& s) : type_(Type::String), str_(s) {}
  Value(const Array& a) : type_(Type::Array), arr_(a) {}
  Value(const Object& o) : type_(Type::Object), obj_(o) {}

  Type type() const { return type_; }
  bool is_object() const { return type_ == Type::Object; }
  bool is_array() const { return type_ == Type::Array; }
  bool is_number() const { return type_ == Type::Number; }

  bool as_bool() const { return bool_; }
  double as_double() const { return num_; }
  long long as_int() const { return (long long)(num_); }
  const std::string& as_string() const { return str_; }
  const Array& as_array() const { return arr_; }
  const Object& as_object() const { return obj_; }

  // Convenience object accessors.
  bool has(const std::string& key) const {
    return type_ == Type::Object && obj_.find(key) != obj_.end();
  }
  const Value& at(const std::string& key) const {
    auto it = obj_.find(key);
    if (it == obj_.end())
      throw std::runtime_error("json: missing key '" + key + "'");
    return it->second;
  }

  // Builders used by the serializer side of the program.
  static Value array() { Value v; v.type_ = Type::Array; return v; }
  static Value object() { Value v; v.type_ = Type::Object; return v; }
  void push_back(const Value& v) { arr_.push_back(v); }
  Value& operator[](const std::string& key) {
    type_ = Type::Object;
    return obj_[key];
  }

  std::string dump(int indent = 2) const {
    std::ostringstream os;
    write(os, indent, 0);
    return os.str();
  }

 private:
  void write(std::ostringstream& os, int indent, int depth) const {
    const std::string pad(indent * (depth + 1), ' ');
    const std::string pad_close(indent * depth, ' ');
    switch (type_) {
      case Type::Null: os << "null"; break;
      case Type::Bool: os << (bool_ ? "true" : "false"); break;
      case Type::Number:
        if (is_int_) {
          os << as_int();
        } else {
          std::ostringstream tmp;
          tmp << num_;
          os << tmp.str();
        }
        break;
      case Type::String: write_string(os, str_); break;
      case Type::Array: {
        if (arr_.empty()) { os << "[]"; break; }
        os << "[\n";
        for (size_t i = 0; i < arr_.size(); ++i) {
          os << pad;
          arr_[i].write(os, indent, depth + 1);
          if (i + 1 < arr_.size()) os << ",";
          os << "\n";
        }
        os << pad_close << "]";
        break;
      }
      case Type::Object: {
        if (obj_.empty()) { os << "{}"; break; }
        os << "{\n";
        size_t i = 0;
        for (const auto& kv : obj_) {
          os << pad;
          write_string(os, kv.first);
          os << ": ";
          kv.second.write(os, indent, depth + 1);
          if (++i < obj_.size()) os << ",";
          os << "\n";
        }
        os << pad_close << "}";
        break;
      }
    }
  }

  static void write_string(std::ostringstream& os, const std::string& s) {
    os << '"';
    for (char c : s) {
      switch (c) {
        case '"': os << "\\\""; break;
        case '\\': os << "\\\\"; break;
        case '\n': os << "\\n"; break;
        case '\t': os << "\\t"; break;
        case '\r': os << "\\r"; break;
        default: os << c;
      }
    }
    os << '"';
  }

  Type type_;
  bool bool_ = false;
  double num_ = 0.0;
  bool is_int_ = false;
  std::string str_;
  Array arr_;
  Object obj_;
};

// --- Parser ---------------------------------------------------------------

class Parser {
 public:
  explicit Parser(const std::string& text) : s_(text), i_(0) {}

  Value parse() {
    skip_ws();
    Value v = parse_value();
    skip_ws();
    if (i_ != s_.size()) fail("trailing characters after JSON document");
    return v;
  }

 private:
  Value parse_value() {
    skip_ws();
    if (i_ >= s_.size()) fail("unexpected end of input");
    char c = s_[i_];
    switch (c) {
      case '{': return parse_object();
      case '[': return parse_array();
      case '"': return Value(parse_string());
      case 't': case 'f': return parse_bool();
      case 'n': return parse_null();
      default: return parse_number();
    }
  }

  Value parse_object() {
    expect('{');
    Object obj;
    skip_ws();
    if (peek() == '}') { ++i_; return Value(obj); }
    while (true) {
      skip_ws();
      std::string key = parse_string();
      skip_ws();
      expect(':');
      obj[key] = parse_value();
      skip_ws();
      char c = next();
      if (c == '}') break;
      if (c != ',') fail("expected ',' or '}' in object");
    }
    return Value(obj);
  }

  Value parse_array() {
    expect('[');
    Array arr;
    skip_ws();
    if (peek() == ']') { ++i_; return Value(arr); }
    while (true) {
      arr.push_back(parse_value());
      skip_ws();
      char c = next();
      if (c == ']') break;
      if (c != ',') fail("expected ',' or ']' in array");
    }
    return Value(arr);
  }

  std::string parse_string() {
    expect('"');
    std::string out;
    while (true) {
      if (i_ >= s_.size()) fail("unterminated string");
      char c = s_[i_++];
      if (c == '"') break;
      if (c == '\\') {
        char e = next();
        switch (e) {
          case '"': out += '"'; break;
          case '\\': out += '\\'; break;
          case '/': out += '/'; break;
          case 'n': out += '\n'; break;
          case 't': out += '\t'; break;
          case 'r': out += '\r'; break;
          case 'b': out += '\b'; break;
          case 'f': out += '\f'; break;
          default: out += e;  // unknown escape: keep literal
        }
      } else {
        out += c;
      }
    }
    return out;
  }

  Value parse_number() {
    size_t start = i_;
    bool is_int = true;
    if (peek() == '-') ++i_;
    while (i_ < s_.size() && std::isdigit((unsigned char)s_[i_])) ++i_;
    if (i_ < s_.size() && s_[i_] == '.') { is_int = false; ++i_;
      while (i_ < s_.size() && std::isdigit((unsigned char)s_[i_])) ++i_; }
    if (i_ < s_.size() && (s_[i_] == 'e' || s_[i_] == 'E')) { is_int = false; ++i_;
      if (i_ < s_.size() && (s_[i_] == '+' || s_[i_] == '-')) ++i_;
      while (i_ < s_.size() && std::isdigit((unsigned char)s_[i_])) ++i_; }
    std::string tok = s_.substr(start, i_ - start);
    if (tok.empty() || tok == "-") fail("invalid number");
    if (is_int) return Value((long long)std::stoll(tok));
    return Value(std::stod(tok));
  }

  Value parse_bool() {
    if (s_.compare(i_, 4, "true") == 0) { i_ += 4; return Value(true); }
    if (s_.compare(i_, 5, "false") == 0) { i_ += 5; return Value(false); }
    fail("invalid literal");
    return Value();
  }

  Value parse_null() {
    if (s_.compare(i_, 4, "null") == 0) { i_ += 4; return Value(nullptr); }
    fail("invalid literal");
    return Value();
  }

  void skip_ws() {
    while (i_ < s_.size() && std::isspace((unsigned char)s_[i_])) ++i_;
  }
  char peek() const { return i_ < s_.size() ? s_[i_] : '\0'; }
  char next() {
    if (i_ >= s_.size()) fail("unexpected end of input");
    return s_[i_++];
  }
  void expect(char c) {
    if (next() != c)
      fail(std::string("expected '") + c + "'");
  }
  [[noreturn]] void fail(const std::string& msg) const {
    throw std::runtime_error("json parse error at offset " +
                             std::to_string(i_) + ": " + msg);
  }

  const std::string& s_;
  size_t i_;
};

inline Value parse(const std::string& text) { return Parser(text).parse(); }

}  // namespace json
}  // namespace ufe

#endif  // UFE_JSON_HPP
