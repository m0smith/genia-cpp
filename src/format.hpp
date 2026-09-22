// R23 section 7 numeric field-format-spec integration.
//
// This is presentation-only machinery. Alignment transforms canonical
// display text; zero-padding/grouping are gated to plain numeral atoms;
// precision rounds each numeric kind's exact value with decimal half-up.
#pragma once

#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>

#include "equality.hpp"
#include "render.hpp"
#include "value.hpp"

namespace genia::format {

struct FormatError {
  std::string message;
};

inline std::string quoted_spec(const std::string& spec) { return "'" + spec + "'"; }

inline size_t parse_count(const std::string& digits, const std::string& spec) {
  // Format widths/precisions are presentation allocations. Keep malformed or
  // unrepresentable counts inside the normalized format diagnostic boundary.
  constexpr size_t kMaxPresentationCount = 1000000;
  size_t result = 0;
  for (char c : digits) {
    if (c < '0' || c > '9' || result > (kMaxPresentationCount - static_cast<size_t>(c - '0')) / 10)
      throw FormatError{"format-error: invalid format spec " + quoted_spec(spec)};
    result = result * 10 + static_cast<size_t>(c - '0');
  }
  return result;
}

inline bool is_plain_numeral(const std::string& text) {
  size_t i = (!text.empty() && text[0] == '-') ? 1 : 0;
  const size_t integer_start = i;
  while (i < text.size() && text[i] >= '0' && text[i] <= '9') ++i;
  if (i == integer_start) return false;
  if (i == text.size()) return true;
  if (text[i++] != '.') return false;
  const size_t fraction_start = i;
  while (i < text.size() && text[i] >= '0' && text[i] <= '9') ++i;
  return i == text.size() && i > fraction_start;
}

inline std::string display_text(const value::Value& input) {
  if (input.kind == value::Kind::String) return input.text;
  auto rendered = render::display(input);
  if (!rendered.has_value()) throw FormatError{"format-error: value is not renderable"};
  return *rendered;
}

inline std::pair<bignum::Integer, bignum::Integer> numeric_ratio(const value::Value& input,
                                                                 const std::string& spec) {
  if (equality::is_exact_family_kind(input.kind))
    return equality::exact_family_numerator_denominator(input);
  if (input.kind == value::Kind::Float64) {
    if (!std::isfinite(input.float64))
      throw FormatError{"format-error: format spec " + quoted_spec(spec) +
                        " requires a finite numeric value"};
    return equality::finite_float64_numerator_denominator(input.float64);
  }
  throw FormatError{"format-error: format spec " + quoted_spec(spec) +
                    " requires string or numeric value"};
}

inline std::string precision_text(const value::Value& input, size_t precision,
                                  const std::string& spec) {
  auto [numerator, denominator] = numeric_ratio(input, spec);
  const bool negative = !numerator.is_positive_or_zero();
  bignum::Integer scaled = numerator.absolute();
  const bignum::Integer ten = bignum::Integer::from_u64(10);
  for (size_t i = 0; i < precision; ++i) scaled = scaled.mul(ten);
  auto [quotient, remainder] = bignum::Integer::divmod_positive(scaled, denominator);
  if (bignum::Integer::compare(remainder.shift_left(1), denominator) >= 0)
    quotient = quotient.add(bignum::Integer::from_u64(1));
  std::string digits = quotient.to_decimal_string();
  std::string result;
  if (precision == 0) {
    result = digits;
  } else {
    if (digits.size() <= precision) digits.insert(0, precision + 1 - digits.size(), '0');
    result = digits.substr(0, digits.size() - precision) + "." +
             digits.substr(digits.size() - precision);
  }
  return negative ? "-" + result : result;
}

inline std::string apply_spec(const value::Value& input, const std::string& spec) {
  if (spec.empty()) throw FormatError{"format-error: invalid format spec ''"};
  if (spec[0] == '<' || spec[0] == '>' || spec[0] == '^') {
    const std::string digits = spec.substr(1);
    if (digits.empty()) throw FormatError{"format-error: invalid format spec " + quoted_spec(spec)};
    const size_t width = parse_count(digits, spec);
    std::string text = display_text(input);
    if (text.size() >= width) return text;
    const size_t padding = width - text.size();
    if (spec[0] == '<') return text + std::string(padding, ' ');
    if (spec[0] == '>') return std::string(padding, ' ') + text;
    const size_t left = padding / 2;
    return std::string(left, ' ') + text + std::string(padding - left, ' ');
  }
  if (spec[0] == '.') {
    const std::string digits = spec.substr(1);
    if (digits.empty()) throw FormatError{"format-error: invalid format spec " + quoted_spec(spec)};
    const size_t precision = parse_count(digits, spec);
    if (input.kind == value::Kind::String) return input.text.substr(0, precision);
    return precision_text(input, precision, spec);
  }
  if (spec[0] == '0' && spec.size() > 1 &&
      spec.find_first_not_of("0123456789", 1) == std::string::npos) {
    const size_t width = parse_count(spec, spec);
    if (!equality::is_numeric_kind(input.kind))
      throw FormatError{"format-error: format spec " + quoted_spec(spec) +
                        " requires numeric value"};
    std::string text = display_text(input);
    if (!is_plain_numeral(text))
      throw FormatError{"format-error: format spec " + quoted_spec(spec) +
                        " is not supported for this numeric representation"};
    if (text.size() >= width) return text;
    const size_t padding = width - text.size();
    if (!text.empty() && text[0] == '-') return "-" + std::string(padding, '0') + text.substr(1);
    return std::string(padding, '0') + text;
  }
  if (spec == ",") {
    if (!equality::is_numeric_kind(input.kind))
      throw FormatError{"format-error: format spec ',' requires numeric value"};
    std::string text = display_text(input);
    if (!is_plain_numeral(text))
      throw FormatError{
          "format-error: format spec ',' is not supported for this numeric representation"};
    const bool negative = text[0] == '-';
    if (negative) text.erase(0, 1);
    const size_t point = text.find('.');
    const size_t integer_size = point == std::string::npos ? text.size() : point;
    std::string grouped;
    for (size_t i = 0; i < integer_size; ++i) {
      if (i > 0 && (integer_size - i) % 3 == 0) grouped.push_back(',');
      grouped.push_back(text[i]);
    }
    if (point != std::string::npos) grouped += text.substr(point);
    return negative ? "-" + grouped : grouped;
  }
  throw FormatError{"format-error: unsupported format spec " + quoted_spec(spec)};
}

inline const value::Value* lookup_field(const value::OrderedMap& fields, const std::string& name) {
  for (const auto& [key, mapped] : fields.items())
    if (key.kind == value::Kind::String && key.text == name) return &mapped;
  return nullptr;
}

inline bool simple_field_name(const std::string& name) {
  if (name.empty()) return false;
  for (char c : name)
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
          c == '-' || c == '?' || c == '!'))
      return false;
  return true;
}

// Returns nullopt for templates outside this bounded increment: plain
// substitution, positional fields, nested field paths, and general format
// syntax remain honestly unsupported rather than being partially emulated.
inline std::optional<std::string> render_template(const std::string& input,
                                                  const value::OrderedMap& fields) {
  std::string output;
  bool saw_field_spec = false;
  for (size_t i = 0; i < input.size();) {
    if (input[i] == '{' && i + 1 < input.size() && input[i + 1] == '{') {
      output.push_back('{');
      i += 2;
      continue;
    }
    if (input[i] == '}' && i + 1 < input.size() && input[i + 1] == '}') {
      output.push_back('}');
      i += 2;
      continue;
    }
    if (input[i] != '{') {
      output.push_back(input[i++]);
      continue;
    }
    const size_t close = input.find('}', i + 1);
    if (close == std::string::npos) return std::nullopt;
    const std::string field = input.substr(i + 1, close - i - 1);
    const size_t colon = field.find(':');
    const std::string name = field.substr(0, colon);
    if (colon == std::string::npos || !simple_field_name(name) ||
        (name[0] >= '0' && name[0] <= '9'))
      return std::nullopt;
    saw_field_spec = true;
    const value::Value* resolved = lookup_field(fields, name);
    if (resolved == nullptr) return std::nullopt;
    output += apply_spec(*resolved, field.substr(colon + 1));
    i = close + 1;
  }
  return saw_field_spec ? std::optional<std::string>(std::move(output)) : std::nullopt;
}

}  // namespace genia::format
