// R23 strict generic JSON numeric boundary.
#pragma once

#include <charconv>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "arithmetic.hpp"
#include "equality.hpp"
#include "float64.hpp"
#include "render.hpp"
#include "value.hpp"

namespace genia::strict_json {

enum class Error { NumberOutOfRange, UnsupportedValue, InvalidJson };

struct Result {
  std::optional<value::Value> value;
  Error error = Error::InvalidJson;
};

inline Result ok(value::Value value) { return {std::move(value), Error::InvalidJson}; }
inline Result fail(Error error) { return {std::nullopt, error}; }

inline const char* reason(Error error) {
  switch (error) {
    case Error::NumberOutOfRange:
      return "json_number_out_of_range";
    case Error::UnsupportedValue:
      return "unsupported_json_value";
    case Error::InvalidJson:
      return "invalid_json";
  }
  return "invalid_json";
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

inline Result encode_value(const value::Value& input);

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

inline Result encode_value(const value::Value& input) {
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
    case value::Kind::List: {
      std::string text = "[";
      for (size_t i = 0; i < input.list_items->size(); ++i) {
        auto encoded = encode_value((*input.list_items)[i]);
        if (!encoded.value.has_value()) return encoded;
        if (i != 0) text += ",";
        text += encoded.value->text;
      }
      return ok(value::Value::make_string(text + "]"));
    }
    case value::Kind::Map: {
      std::string text = "{";
      bool first = true;
      for (const auto& [key, mapped] : input.map->items()) {
        if (key.kind != value::Kind::String) return fail(Error::UnsupportedValue);
        auto encoded = encode_value(mapped);
        if (!encoded.value.has_value()) return encoded;
        if (!first) text += ",";
        first = false;
        text += escape_string(key.text) + ":" + encoded.value->text;
      }
      return ok(value::Value::make_string(text + "}"));
    }
    default:
      return fail(Error::UnsupportedValue);
  }
}

inline Result decode_number(const std::string& text) {
  if (text.find_first_of(".eE") == std::string::npos) {
    bool negative = !text.empty() && text[0] == '-';
    const std::string_view digits(text.data() + (negative ? 1 : 0),
                                  text.size() - (negative ? 1 : 0));
    if (digits.empty() || (digits.size() > 1 && digits[0] == '0')) return fail(Error::InvalidJson);
    auto parsed = bignum::Integer::from_unsigned_decimal(std::string(digits));
    if (!parsed.has_value()) return fail(Error::InvalidJson);
    if (negative) *parsed = parsed->negate();
    if (!safe_integer(*parsed)) return fail(Error::NumberOutOfRange);
    return ok(value::Value::make_integer(*parsed));
  }
  auto decimal = parse_decimal_token(text);
  if (!decimal.has_value()) return fail(Error::InvalidJson);
  if (!stable_decimal(*decimal)) return fail(Error::NumberOutOfRange);
  return ok(*decimal);
}

}  // namespace genia::strict_json
