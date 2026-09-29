// R26-2 strict generic JSON data-bridge boundary (genia-2026 issue
// #1024, docs/design/r26-cpp-data-bridge-contract.md). Widens R24/E24-7's
// scalar-numeric-only slice (kept, unchanged -- see `parse_decimal_token`/
// `stable_decimal`/`safe_integer`/`float64_json_text` below, reused
// verbatim) to the full grammar: objects, arrays, strings/Unicode,
// booleans, null, nesting/duplicate-key limits, deterministic
// sorted-key/indented encode layout, and the pinned reason/context
// vocabulary section 3.10 requires.
#pragma once

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "arithmetic.hpp"
#include "equality.hpp"
#include "float64.hpp"
#include "render.hpp"
#include "utf8.hpp"
#include "value.hpp"

namespace genia::strict_json {

enum class Error {
  InvalidJson,
  DuplicateKey,
  NumberOutOfRange,
  InvalidUnicode,
  NestingTooDeep,
  UnsupportedValue,
};

// Extra detail beyond the bare reason, used to populate the failure
// Outcome's context map (contract section 3.10).
struct Failure {
  Error error = Error::InvalidJson;
  int line = 0;
  int column = 0;
  bool has_line_column = false;
  std::string key;         // DuplicateKey
  std::string value_type;  // UnsupportedValue

  Failure() = default;
  explicit Failure(Error err) : error(err) {}
};

struct Result {
  std::optional<value::Value> value;
  Failure failure;
};

inline Result ok(value::Value value) { return {std::move(value), Failure{}}; }
inline Result fail(Failure failure) { return {std::nullopt, failure}; }
inline Result fail(Error error) { return fail(Failure{error}); }

inline const char* reason(Error error) {
  switch (error) {
    case Error::InvalidJson:
      return "invalid_json";
    case Error::DuplicateKey:
      return "duplicate_json_key";
    case Error::NumberOutOfRange:
      return "json_number_out_of_range";
    case Error::InvalidUnicode:
      return "invalid_json_unicode";
    case Error::NestingTooDeep:
      return "json_nesting_too_deep";
    case Error::UnsupportedValue:
      return "unsupported_json_value";
  }
  return "invalid_json";
}

inline const char* reason(const Failure& failure) { return reason(failure.error); }

constexpr int kMaxNesting = 128;

// Builds the pinned context shape (contract section 3.10): `kind`/
// `operation`/`status`/`reason` always present as plain strings (this
// slice has no Symbol value kind, so genia-2026's `symbol(...)` wrapping
// is represented as an ordinary String -- `display`/`get` observe it
// identically for every case this host's evidence exercises), plus
// `key`/`line`/`column`/`value_type` only when applicable.
inline value::Value build_context(const std::string& operation, const std::string& status,
                                  const Failure& failure) {
  auto map = std::make_shared<value::OrderedMap>();
  auto put = [&](const std::string& key, value::Value val) {
    auto key_value = value::Value::make_string(key);
    map->put(equality::map_key_encoding(key_value), key_value, std::move(val));
  };
  put("kind", value::Value::make_string("json"));
  put("operation", value::Value::make_string(operation));
  put("status", value::Value::make_string(status));
  put("reason", value::Value::make_string(reason(failure)));
  if (failure.has_line_column) {
    put("line", value::Value::make_integer(
                    bignum::Integer::from_u64(static_cast<std::uint64_t>(failure.line))));
    put("column", value::Value::make_integer(
                      bignum::Integer::from_u64(static_cast<std::uint64_t>(failure.column))));
  }
  if (!failure.key.empty() || failure.error == Error::DuplicateKey) {
    put("key", value::Value::make_string(failure.key));
  }
  if (!failure.value_type.empty()) {
    put("value_type", value::Value::make_string(failure.value_type));
  }
  return value::Value::make_map(map);
}

inline value::Value success_context(const std::string& operation, const std::string& status) {
  Failure placeholder{};
  auto map = std::make_shared<value::OrderedMap>();
  auto put = [&](const std::string& key, value::Value val) {
    auto key_value = value::Value::make_string(key);
    map->put(equality::map_key_encoding(key_value), key_value, std::move(val));
  };
  put("kind", value::Value::make_string("json"));
  put("operation", value::Value::make_string(operation));
  put("status", value::Value::make_string(status));
  put("reason", value::Value::make_string(status));
  (void)placeholder;
  return value::Value::make_map(map);
}

inline bignum::Integer signed_digits(std::string_view digits, bool negative) {
  auto parsed = bignum::Integer::from_unsigned_decimal(std::string(digits));
  if (!parsed.has_value()) return bignum::Integer();
  return negative ? parsed->negate() : *parsed;
}

// Lexical JSON fraction/exponent token -> exact canonical Decimal. No host
// binary floating-point value participates in this conversion.
inline std::optional<value::Value> parse_decimal_token(std::string_view token) {
  size_t pos = 0;
  bool negative = false;
  if (pos < token.size() && token[pos] == '-') {
    negative = true;
    ++pos;
  }
  const size_t integer_start = pos;
  if (pos >= token.size() || token[pos] < '0' || token[pos] > '9') return std::nullopt;
  if (token[pos] == '0') {
    ++pos;
    if (pos < token.size() && token[pos] >= '0' && token[pos] <= '9') return std::nullopt;
  } else {
    while (pos < token.size() && token[pos] >= '0' && token[pos] <= '9') ++pos;
  }
  const std::string integer_digits(token.substr(integer_start, pos - integer_start));
  std::string fractional_digits;
  bool has_fraction = false;
  if (pos < token.size() && token[pos] == '.') {
    has_fraction = true;
    ++pos;
    const size_t start = pos;
    while (pos < token.size() && token[pos] >= '0' && token[pos] <= '9') ++pos;
    if (pos == start) return std::nullopt;
    fractional_digits = std::string(token.substr(start, pos - start));
  }
  int64_t explicit_exponent = 0;
  bool has_exponent = false;
  if (pos < token.size() && (token[pos] == 'e' || token[pos] == 'E')) {
    has_exponent = true;
    ++pos;
    bool exponent_negative = false;
    if (pos < token.size() && (token[pos] == '+' || token[pos] == '-')) {
      exponent_negative = token[pos] == '-';
      ++pos;
    }
    const size_t start = pos;
    uint64_t magnitude = 0;
    while (pos < token.size() && token[pos] >= '0' && token[pos] <= '9') {
      const unsigned digit = static_cast<unsigned>(token[pos] - '0');
      if (magnitude > 1000000) return std::nullopt;
      magnitude = magnitude * 10 + digit;
      ++pos;
    }
    if (pos == start || magnitude > 1000000) return std::nullopt;
    explicit_exponent =
        exponent_negative ? -static_cast<int64_t>(magnitude) : static_cast<int64_t>(magnitude);
  }
  if (pos != token.size() || (!has_fraction && !has_exponent)) return std::nullopt;
  const std::string coefficient_digits = integer_digits + fractional_digits;
  auto coefficient = bignum::Integer::from_unsigned_decimal(coefficient_digits);
  if (!coefficient.has_value()) return std::nullopt;
  if (negative) *coefficient = coefficient->negate();
  const int64_t exponent = explicit_exponent - static_cast<int64_t>(fractional_digits.size());
  auto [canonical_coefficient, canonical_exponent] =
      arithmetic::canonicalize_decimal(*coefficient, exponent);
  return value::Value::make_decimal(canonical_coefficient, canonical_exponent);
}

inline std::string float64_json_text(double number) {
  const std::string atom = render::render_float64(number);
  return atom.substr(8, atom.size() - 9);
}

inline bool stable_decimal(const value::Value& decimal) {
  const auto [numerator, denominator] = equality::exact_family_numerator_denominator(decimal);
  auto converted = float64::fraction_to_binary64(numerator, denominator);
  if (!converted.has_value() || !std::isfinite(*converted)) return false;
  if (!numerator.is_zero() && *converted == 0.0) return false;
  auto shortest = parse_decimal_token(float64_json_text(*converted));
  if (!shortest.has_value()) return false;
  return equality::exact_family_equal(decimal, *shortest);
}

inline bool safe_integer(const bignum::Integer& integer) {
  static const bignum::Integer bound = *bignum::Integer::from_unsigned_decimal("9007199254740991");
  return bignum::Integer::compare(integer, bound) <= 0 &&
         bignum::Integer::compare(integer, bound.negate()) >= 0;
}

// ---------------------------------------------------------------------
// Decode
// ---------------------------------------------------------------------

class Decoder {
 public:
  explicit Decoder(const std::string& text) : text_(text) {}

  Result parse_document() {
    skip_ws();
    auto result = parse_value(0);
    if (!result.value.has_value()) return result;
    skip_ws();
    if (pos_ != text_.size()) {
      return fail(invalid_here());
    }
    return result;
  }

 private:
  const std::string& text_;
  size_t pos_ = 0;
  int line_ = 1;
  int column_ = 1;

  const unsigned char* bytes() const {
    return reinterpret_cast<const unsigned char*>(text_.data());
  }

  bool at_end() const { return pos_ >= text_.size(); }

  // Structural ASCII lookahead (all JSON structural characters and
  // digits are single-byte ASCII); does not advance.
  char peek_byte() const { return at_end() ? '\0' : text_[pos_]; }

  Failure invalid_here() const {
    Failure failure{Error::InvalidJson};
    failure.has_line_column = true;
    failure.line = line_;
    failure.column = column_;
    return failure;
  }

  // Advances over one already-validated codepoint of `length` bytes
  // starting at `pos_`, updating line/column (column counts codepoints
  // within the current line, per contract section 3.7).
  void advance_codepoint(std::uint32_t codepoint, std::size_t length) {
    pos_ += length;
    if (codepoint == '\n') {
      ++line_;
      column_ = 1;
    } else {
      ++column_;
    }
  }

  void advance_ascii() { advance_codepoint(static_cast<unsigned char>(text_[pos_]), 1); }

  void skip_ws() {
    while (!at_end()) {
      const char c = text_[pos_];
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        advance_ascii();
        continue;
      }
      break;
    }
  }

  bool consume_literal(const char* literal) {
    const size_t len = std::char_traits<char>::length(literal);
    if (text_.compare(pos_, len, literal) != 0) return false;
    for (size_t i = 0; i < len; ++i) advance_ascii();
    return true;
  }

  Result parse_value(int depth) {
    skip_ws();
    if (at_end()) return fail(invalid_here());
    const char c = peek_byte();
    if (c == '{') return parse_object(depth);
    if (c == '[') return parse_array(depth);
    if (c == '"') return parse_string();
    if (c == 't') {
      const Failure at = invalid_here();
      if (!consume_literal("true")) return fail(at);
      return ok(value::Value::make_boolean(true));
    }
    if (c == 'f') {
      const Failure at = invalid_here();
      if (!consume_literal("false")) return fail(at);
      return ok(value::Value::make_boolean(false));
    }
    if (c == 'n') {
      const Failure at = invalid_here();
      if (!consume_literal("null")) return fail(at);
      return ok(value::Value::make_outcome_none(value::Value::make_string("nil")));
    }
    if (c == '-' || (c >= '0' && c <= '9')) return parse_number();
    return fail(invalid_here());
  }

  Result parse_number() {
    const size_t start = pos_;
    const Failure at = invalid_here();
    bool has_fraction_or_exponent = false;
    if (peek_byte() == '-') advance_ascii();
    if (at_end() || peek_byte() < '0' || peek_byte() > '9') return fail(at);
    if (peek_byte() == '0') {
      advance_ascii();
    } else {
      while (!at_end() && peek_byte() >= '0' && peek_byte() <= '9') advance_ascii();
    }
    if (!at_end() && peek_byte() == '.') {
      has_fraction_or_exponent = true;
      advance_ascii();
      if (at_end() || peek_byte() < '0' || peek_byte() > '9') return fail(at);
      while (!at_end() && peek_byte() >= '0' && peek_byte() <= '9') advance_ascii();
    }
    if (!at_end() && (peek_byte() == 'e' || peek_byte() == 'E')) {
      has_fraction_or_exponent = true;
      advance_ascii();
      if (!at_end() && (peek_byte() == '+' || peek_byte() == '-')) advance_ascii();
      if (at_end() || peek_byte() < '0' || peek_byte() > '9') return fail(at);
      while (!at_end() && peek_byte() >= '0' && peek_byte() <= '9') advance_ascii();
    }
    const std::string token = text_.substr(start, pos_ - start);
    if (!has_fraction_or_exponent) {
      const bool negative = !token.empty() && token[0] == '-';
      const std::string_view digits(token.data() + (negative ? 1 : 0),
                                    token.size() - (negative ? 1 : 0));
      auto parsed = bignum::Integer::from_unsigned_decimal(std::string(digits));
      if (!parsed.has_value()) return fail(at);
      if (negative) *parsed = parsed->negate();
      if (!safe_integer(*parsed)) {
        Failure failure{Error::NumberOutOfRange};
        return fail(failure);
      }
      return ok(value::Value::make_integer(*parsed));
    }
    auto decimal = parse_decimal_token(token);
    if (!decimal.has_value()) return fail(at);
    if (!stable_decimal(*decimal)) {
      Failure failure{Error::NumberOutOfRange};
      return fail(failure);
    }
    return ok(*decimal);
  }

  // Parses one JSON string token (the current byte must be '"'),
  // returning its decoded UTF-8 text. Validates every scalar (raw or
  // `\uXXXX`-escaped) is not an encoded surrogate half (contract section
  // 3.2's Unicode requirement) and that no unescaped control character
  // (< U+0020) appears, matching Python's own JSON scanner.
  Result parse_string() {
    const Failure open_at = invalid_here();
    advance_ascii();  // opening quote
    std::string out;
    while (true) {
      if (at_end()) return fail(open_at);
      const unsigned char c = static_cast<unsigned char>(text_[pos_]);
      if (c == '"') {
        advance_ascii();
        return ok(value::Value::make_string(std::move(out)));
      }
      if (c == '\\') {
        const Failure escape_at = invalid_here();
        advance_ascii();
        if (at_end()) return fail(escape_at);
        const char esc = text_[pos_];
        switch (esc) {
          case '"':
            out.push_back('"');
            advance_ascii();
            break;
          case '\\':
            out.push_back('\\');
            advance_ascii();
            break;
          case '/':
            out.push_back('/');
            advance_ascii();
            break;
          case 'b':
            out.push_back('\b');
            advance_ascii();
            break;
          case 'f':
            out.push_back('\f');
            advance_ascii();
            break;
          case 'n':
            out.push_back('\n');
            advance_ascii();
            break;
          case 'r':
            out.push_back('\r');
            advance_ascii();
            break;
          case 't':
            out.push_back('\t');
            advance_ascii();
            break;
          case 'u': {
            advance_ascii();  // 'u'
            auto first = read_hex4();
            if (!first.has_value()) return fail(escape_at);
            std::uint32_t codepoint = *first;
            if (codepoint >= 0xD800 && codepoint <= 0xDBFF && pos_ + 1 < text_.size() &&
                text_[pos_] == '\\' && text_[pos_ + 1] == 'u') {
              const size_t save_pos = pos_;
              const int save_line = line_;
              const int save_column = column_;
              advance_ascii();
              advance_ascii();
              auto second = read_hex4();
              if (second.has_value() && *second >= 0xDC00 && *second <= 0xDFFF) {
                codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (*second - 0xDC00);
              } else {
                pos_ = save_pos;
                line_ = save_line;
                column_ = save_column;
              }
            }
            if (codepoint >= 0xD800 && codepoint <= 0xDFFF) {
              return fail(Failure{Error::InvalidUnicode});
            }
            utf8::encode_codepoint(codepoint, out);
            break;
          }
          default:
            return fail(escape_at);
        }
        continue;
      }
      if (c < 0x20) {
        return fail(invalid_here());
      }
      auto decoded = utf8::decode_one_permissive(bytes(), pos_, text_.size());
      if (!decoded.has_value()) return fail(invalid_here());
      if (decoded->codepoint >= 0xD800 && decoded->codepoint <= 0xDFFF) {
        return fail(Failure{Error::InvalidUnicode});
      }
      out.append(text_, pos_, decoded->length);
      advance_codepoint(decoded->codepoint, decoded->length);
    }
  }

  std::optional<std::uint32_t> read_hex4() {
    if (pos_ + 4 > text_.size()) return std::nullopt;
    std::uint32_t value = 0;
    for (int k = 0; k < 4; ++k) {
      const char hc = text_[pos_];
      value <<= 4;
      if (hc >= '0' && hc <= '9') {
        value |= static_cast<std::uint32_t>(hc - '0');
      } else if (hc >= 'a' && hc <= 'f') {
        value |= static_cast<std::uint32_t>(hc - 'a' + 10);
      } else if (hc >= 'A' && hc <= 'F') {
        value |= static_cast<std::uint32_t>(hc - 'A' + 10);
      } else {
        return std::nullopt;
      }
      advance_ascii();
    }
    return value;
  }

  Result parse_array(int depth) {
    const int next_depth = depth + 1;
    if (next_depth > kMaxNesting) return fail(Error::NestingTooDeep);
    advance_ascii();  // '['
    std::vector<value::Value> items;
    skip_ws();
    if (!at_end() && peek_byte() == ']') {
      advance_ascii();
      return ok(value::Value::make_list(std::move(items)));
    }
    while (true) {
      auto item = parse_value(next_depth);
      if (!item.value.has_value()) return item;
      items.push_back(std::move(*item.value));
      skip_ws();
      if (at_end()) return fail(invalid_here());
      if (peek_byte() == ',') {
        advance_ascii();
        skip_ws();
        continue;
      }
      if (peek_byte() == ']') {
        advance_ascii();
        return ok(value::Value::make_list(std::move(items)));
      }
      return fail(invalid_here());
    }
  }

  Result parse_object(int depth) {
    const int next_depth = depth + 1;
    if (next_depth > kMaxNesting) return fail(Error::NestingTooDeep);
    advance_ascii();  // '{'
    std::vector<std::pair<std::string, value::Value>> pairs;
    skip_ws();
    auto finish = [&]() -> Result {
      auto map = std::make_shared<value::OrderedMap>();
      for (auto& [key, mapped] : pairs) {
        auto key_value = value::Value::make_string(key);
        const std::string encoding = equality::map_key_encoding(key_value);
        if (map->has(encoding)) {
          Failure failure{Error::DuplicateKey};
          failure.key = key;
          return fail(failure);
        }
        map->put(encoding, std::move(key_value), std::move(mapped));
      }
      return ok(value::Value::make_map(map));
    };
    if (!at_end() && peek_byte() == '}') {
      advance_ascii();
      return finish();
    }
    while (true) {
      skip_ws();
      if (at_end() || peek_byte() != '"') return fail(invalid_here());
      auto key_result = parse_string();
      if (!key_result.value.has_value()) return key_result;
      skip_ws();
      if (at_end() || peek_byte() != ':') return fail(invalid_here());
      advance_ascii();
      skip_ws();
      auto value_result = parse_value(next_depth);
      if (!value_result.value.has_value()) return value_result;
      pairs.emplace_back(key_result.value->text, std::move(*value_result.value));
      skip_ws();
      if (at_end()) return fail(invalid_here());
      if (peek_byte() == ',') {
        advance_ascii();
        continue;
      }
      if (peek_byte() == '}') {
        advance_ascii();
        return finish();
      }
      return fail(invalid_here());
    }
  }
};

inline Result decode_document(const std::string& text) {
  // Contract section 3.6: a leading BOM is not stripped and is not
  // accepted as insignificant whitespace -- the parser simply never
  // treats U+FEFF as whitespace, so it naturally fails at position
  // (1, 1) exactly like Python's own scanner.
  return Decoder(text).parse_document();
}

// ---------------------------------------------------------------------
// Encode
// ---------------------------------------------------------------------

inline std::string escape_string(const std::string& input) {
  std::string output = "\"";
  constexpr char hex[] = "0123456789abcdef";
  for (unsigned char c : input) {
    switch (c) {
      case '\"':
        output += "\\\"";
        break;
      case '\\':
        output += "\\\\";
        break;
      case '\b':
        output += "\\b";
        break;
      case '\f':
        output += "\\f";
        break;
      case '\n':
        output += "\\n";
        break;
      case '\r':
        output += "\\r";
        break;
      case '\t':
        output += "\\t";
        break;
      default:
        if (c < 0x20) {
          output += "\\u00";
          output.push_back(hex[c >> 4]);
          output.push_back(hex[c & 0x0f]);
        } else {
          output.push_back(static_cast<char>(c));
        }
    }
  }
  output.push_back('\"');
  return output;
}

inline Result encode_at(const value::Value& input, int depth);

inline Result encode_container_items(const std::vector<std::string>& lines, char open, char close,
                                     int depth) {
  if (lines.empty()) {
    return ok(value::Value::make_string(std::string(1, open) + std::string(1, close)));
  }
  const std::string child_indent(static_cast<size_t>((depth + 1) * 2), ' ');
  const std::string close_indent(static_cast<size_t>(depth * 2), ' ');
  std::string text;
  text.push_back(open);
  text.push_back('\n');
  for (size_t i = 0; i < lines.size(); ++i) {
    text += child_indent;
    text += lines[i];
    if (i + 1 != lines.size()) text += ",";
    text += "\n";
  }
  text += close_indent;
  text.push_back(close);
  return ok(value::Value::make_string(std::move(text)));
}

inline Result encode_at(const value::Value& input, int depth) {
  switch (input.kind) {
    case value::Kind::Integer:
      if (!safe_integer(input.integer)) return fail(Error::NumberOutOfRange);
      return ok(value::Value::make_string(input.integer.to_decimal_string()));
    case value::Kind::Decimal:
      if (!stable_decimal(input)) return fail(Error::NumberOutOfRange);
      return ok(value::Value::make_string(
          render::render_decimal(input.decimal_coefficient, input.decimal_exponent)));
    case value::Kind::Rational: {
      if (!arithmetic::denominator_terminates_in_base10(input.rational_denominator))
        return fail(Error::UnsupportedValue);
      const auto decimal = arithmetic::decimal_from_terminating_fraction(
          input.rational_numerator, input.rational_denominator);
      if (!stable_decimal(decimal)) return fail(Error::NumberOutOfRange);
      return ok(value::Value::make_string(
          render::render_decimal(decimal.decimal_coefficient, decimal.decimal_exponent)));
    }
    case value::Kind::Float64:
      if (!std::isfinite(input.float64)) return fail(Error::NumberOutOfRange);
      return ok(value::Value::make_string(float64_json_text(input.float64)));
    case value::Kind::Boolean:
      return ok(value::Value::make_string(input.boolean ? "true" : "false"));
    case value::Kind::String:
      return ok(value::Value::make_string(escape_string(input.text)));
    case value::Kind::Outcome:
      if (!input.outcome_is_err && input.outcome_is_none) {
        return ok(value::Value::make_string("null"));
      }
      return fail(Error::UnsupportedValue);
    case value::Kind::List: {
      const int next_depth = depth + 1;
      if (next_depth > kMaxNesting) return fail(Error::NestingTooDeep);
      std::vector<std::string> lines;
      for (const auto& item : *input.list_items) {
        auto encoded = encode_at(item, next_depth);
        if (!encoded.value.has_value()) return encoded;
        lines.push_back(std::move(encoded.value->text));
      }
      return encode_container_items(lines, '[', ']', depth);
    }
    case value::Kind::Map: {
      const int next_depth = depth + 1;
      if (next_depth > kMaxNesting) return fail(Error::NestingTooDeep);
      std::vector<std::pair<std::string, std::string>> entries;
      for (const auto& [key, mapped] : input.map->items()) {
        if (key.kind != value::Kind::String) {
          Failure failure{Error::UnsupportedValue};
          failure.value_type = "map-key";
          return fail(failure);
        }
        auto encoded = encode_at(mapped, next_depth);
        if (!encoded.value.has_value()) return encoded;
        entries.emplace_back(key.text, std::move(encoded.value->text));
      }
      std::sort(entries.begin(), entries.end(),
                [](const auto& a, const auto& b) { return a.first < b.first; });
      std::vector<std::string> lines;
      lines.reserve(entries.size());
      for (auto& [key, encoded_value] : entries) {
        lines.push_back(escape_string(key) + ": " + encoded_value);
      }
      return encode_container_items(lines, '{', '}', depth);
    }
    default:
      return fail(Error::UnsupportedValue);
  }
}

// Top-level entry point: consumes exactly one outer `json`-represented
// layer (contract section 3.1); a nested `Represented` value anywhere
// deeper is genuinely unsupported (encode_at has no Represented case).
inline Result encode_value(const value::Value& input) {
  if (input.kind == value::Kind::Represented) {
    if (input.represented_facet != "json") {
      Failure failure{Error::UnsupportedValue};
      failure.value_type = "represented";
      return fail(failure);
    }
    return encode_at(*input.represented_value, 0);
  }
  return encode_at(input, 0);
}

inline Result decode_number(const std::string& text) { return decode_document(text); }

}  // namespace genia::strict_json
