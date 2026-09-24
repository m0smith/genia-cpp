// R18 portable value equality for the E24-2..E24-7 vertical slice's
// supported kinds (Integer, Decimal, Rational, Boolean, String, Bytes,
// List; Map is compared structurally too though no pinned evidence
// exercises it).
//
// docs/design/r18-portable-value-equality-contract.md: equality is ONE
// relation (==, !=), implemented as a single internal dispatch over
// Genia semantic kinds -- never falling back to host-language equality
// for a kind this dispatch does not recognize, and never letting a host
// container's own key rules decide map-key identity (see
// map_key_encoding below, which every OrderedMap operation in
// evaluator.hpp routes through). Different kinds compare unequal,
// except R22 section 10.1's "Exact family" bridge (Integer/Decimal/
// Rational compare by mathematical value in every pairing -- see
// `exact_family_equal` below), which extends R18's own pre-R22
// Integer/"float" numeric-equality bridge ("Numeric equality" table).
// This was required as soon as Decimal literals parse at all (E24-7
// increment 1 discovered this the hard way: enabling Decimal literals
// without it turned two previously-honestly-`unsupported`
// spec/eval/r18-*.yaml cases into wrong `ok` results). Increment 7 extends
// that same relation to Float64 comparison and numeric map-key identity.
#pragma once

#include <bit>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "bignum.hpp"
#include "value.hpp"

namespace genia::equality {

// R22 section 10.1 "Exact family": Integer, Decimal, and Rational
// compare by mathematical value for `==`/`!=`, in every pairing --
// extending R18's own pre-R22 Integer/"float" numeric-equality bridge
// ("Numeric equality" table; R22 section 14: "R18 owns equality/key
// architecture; R22 extends only the numeric cases inside that
// architecture"). Every exact-family value has a unique exact
// numerator/denominator representation (Integer I -> I/1; Decimal
// coefficient*10^exponent -> coefficient/1 when exponent >= 0,
// otherwise coefficient/10^-exponent; Rational is already
// numerator/denominator), so two exact-family values are mathematically
// equal exactly when their numerator/denominator pairs are equal after
// cross-multiplication (`n1*d2 == n2*d1`) -- this never rounds either
// operand through a host binary float, matching the contract's explicit
// prohibition on lossy integer/Decimal-to-float conversion.
inline bool is_exact_family_kind(value::Kind kind) {
  return kind == value::Kind::Integer || kind == value::Kind::Decimal ||
         kind == value::Kind::Rational;
}

inline bool is_numeric_kind(value::Kind kind) {
  return is_exact_family_kind(kind) || kind == value::Kind::Float64;
}

inline std::pair<bignum::Integer, bignum::Integer> exact_family_numerator_denominator(
    const value::Value& v) {
  const bignum::Integer one = bignum::Integer::from_u64(1);
  if (v.kind == value::Kind::Integer) {
    return {v.integer, one};
  }
  if (v.kind == value::Kind::Rational) {
    return {v.rational_numerator, v.rational_denominator};
  }
  // Decimal: coefficient * 10^exponent, as an exact fraction.
  const bignum::Integer ten = bignum::Integer::from_u64(10);
  if (v.decimal_exponent >= 0) {
    bignum::Integer numerator = v.decimal_coefficient;
    for (int64_t i = 0; i < v.decimal_exponent; ++i) {
      numerator = numerator.mul(ten);
    }
    return {numerator, one};
  }
  bignum::Integer denominator = one;
  for (int64_t i = 0; i < -v.decimal_exponent; ++i) {
    denominator = denominator.mul(ten);
  }
  return {v.decimal_coefficient, denominator};
}

inline bool exact_family_equal(const value::Value& a, const value::Value& b) {
  const auto [numerator_a, denominator_a] = exact_family_numerator_denominator(a);
  const auto [numerator_b, denominator_b] = exact_family_numerator_denominator(b);
  return bignum::Integer::compare(numerator_a.mul(denominator_b), numerator_b.mul(denominator_a)) ==
         0;
}

// R22 section 10.1: "Integer, Decimal, and Rational compare by
// mathematical value for ==, !=, <, <=, >, and >=." Same
// numerator/denominator-pair + cross-multiplication technique as
// `exact_family_equal`, extended to ordering: since
// `exact_family_numerator_denominator` always produces a strictly
// positive denominator for every exact-family kind (Integer: 1;
// Decimal: a positive power of 10; Rational: positive by
// canonicalization), `n_a/d_a` compares to `n_b/d_b` exactly as
// `n_a*d_b` compares to `n_b*d_a`, with no sign-flip caveat. Returns a
// negative/zero/positive int mirroring `bignum::Integer::compare`.
inline int exact_family_compare(const value::Value& a, const value::Value& b) {
  const auto [numerator_a, denominator_a] = exact_family_numerator_denominator(a);
  const auto [numerator_b, denominator_b] = exact_family_numerator_denominator(b);
  return bignum::Integer::compare(numerator_a.mul(denominator_b), numerator_b.mul(denominator_a));
}

// Exact numerator/denominator for a finite IEEE-754 binary64 value.
// This decodes the represented dyadic value directly from its bits;
// the exact operand in a mixed comparison is never converted to double.
inline std::pair<bignum::Integer, bignum::Integer> finite_float64_numerator_denominator(double v) {
  const uint64_t bits = std::bit_cast<uint64_t>(v);
  const uint64_t fraction = bits & ((uint64_t{1} << 52) - 1);
  const int biased = static_cast<int>((bits >> 52) & 0x7ffu);
  if (biased == 0 && fraction == 0) {
    return {bignum::Integer(), bignum::Integer::from_u64(1)};
  }
  const uint64_t significand = biased == 0 ? fraction : ((uint64_t{1} << 52) | fraction);
  const int exponent2 = biased == 0 ? -1074 : biased - 1023 - 52;
  bignum::Integer numerator = bignum::Integer::from_u64(significand);
  bignum::Integer denominator = bignum::Integer::from_u64(1);
  if (exponent2 >= 0) {
    numerator = numerator.shift_left(static_cast<size_t>(exponent2));
  } else {
    denominator = denominator.shift_left(static_cast<size_t>(-exponent2));
  }
  if ((bits >> 63) != 0) numerator = numerator.negate();
  return {numerator, denominator};
}

enum class NumericOrder : std::uint8_t { Less, Equal, Greater, Unordered };

inline NumericOrder numeric_compare(const value::Value& a, const value::Value& b) {
  if (!is_numeric_kind(a.kind) || !is_numeric_kind(b.kind)) return NumericOrder::Unordered;
  if ((a.kind == value::Kind::Float64 && std::isnan(a.float64)) ||
      (b.kind == value::Kind::Float64 && std::isnan(b.float64))) {
    return NumericOrder::Unordered;
  }
  if (a.kind == value::Kind::Float64 && std::isinf(a.float64)) {
    if (b.kind == value::Kind::Float64 && std::isinf(b.float64) &&
        std::signbit(a.float64) == std::signbit(b.float64)) {
      return NumericOrder::Equal;
    }
    return std::signbit(a.float64) ? NumericOrder::Less : NumericOrder::Greater;
  }
  if (b.kind == value::Kind::Float64 && std::isinf(b.float64)) {
    return std::signbit(b.float64) ? NumericOrder::Greater : NumericOrder::Less;
  }
  const auto [numerator_a, denominator_a] = a.kind == value::Kind::Float64
                                                ? finite_float64_numerator_denominator(a.float64)
                                                : exact_family_numerator_denominator(a);
  const auto [numerator_b, denominator_b] = b.kind == value::Kind::Float64
                                                ? finite_float64_numerator_denominator(b.float64)
                                                : exact_family_numerator_denominator(b);
  const int comparison =
      bignum::Integer::compare(numerator_a.mul(denominator_b), numerator_b.mul(denominator_a));
  if (comparison < 0) return NumericOrder::Less;
  if (comparison > 0) return NumericOrder::Greater;
  return NumericOrder::Equal;
}

// One canonical identity for every legal numeric key. Reducing the
// exact fraction makes Integer/Decimal/Rational/finite-Float64 keys
// collide iff the ordinary numeric equality relation says they are equal.
inline std::optional<std::string> numeric_map_key_encoding(const value::Value& key) {
  if (key.kind == value::Kind::Float64) {
    if (std::isnan(key.float64)) return std::nullopt;
    if (std::isinf(key.float64)) return std::signbit(key.float64) ? "N:-inf" : "N:+inf";
  }
  auto [numerator, denominator] = key.kind == value::Kind::Float64
                                      ? finite_float64_numerator_denominator(key.float64)
                                      : exact_family_numerator_denominator(key);
  const bignum::Integer divisor = bignum::Integer::gcd(numerator, denominator);
  auto reduced_numerator = numerator.exact_divide(divisor);
  auto reduced_denominator = denominator.exact_divide(divisor);
  if (!reduced_numerator.has_value() || !reduced_denominator.has_value()) return std::nullopt;
  numerator = std::move(*reduced_numerator);
  denominator = std::move(*reduced_denominator);
  return "N:" + numerator.to_decimal_string() + "/" + denominator.to_decimal_string();
}

inline std::optional<std::string> map_key_encoding_checked(const value::Value& key) {
  if (is_numeric_kind(key.kind)) return numeric_map_key_encoding(key);
  switch (key.kind) {
    case value::Kind::Boolean:
      return std::string("B:") + (key.boolean ? "1" : "0");
    case value::Kind::String:
      return "S:" + key.text;
    default:
      return std::nullopt;
  }
}

inline std::string map_key_encoding(const value::Value& key) {
  auto encoding = map_key_encoding_checked(key);
  return encoding.value_or(std::string());
}

inline bool structural_equal(const value::Value& a, const value::Value& b) {
  if (is_numeric_kind(a.kind) && is_numeric_kind(b.kind)) {
    return numeric_compare(a, b) == NumericOrder::Equal;
  }
  if (a.kind != b.kind) {
    return false;
  }
  switch (a.kind) {
    case value::Kind::Integer:
    case value::Kind::Decimal:
    case value::Kind::Rational:
      return exact_family_equal(a, b);  // unreachable: handled above, kept for switch coverage
    case value::Kind::Float64:
      return false;  // unreachable: all numeric pairs are handled above
    case value::Kind::Boolean:
      return a.boolean == b.boolean;
    case value::Kind::String:
    case value::Kind::Bytes:
      return a.text == b.text;
    case value::Kind::List: {
      const auto& left_items = *a.list_items;
      const auto& right_items = *b.list_items;
      if (left_items.size() != right_items.size()) {
        return false;
      }
      for (size_t i = 0; i < left_items.size(); ++i) {
        if (!structural_equal(left_items[i], right_items[i])) {
          return false;
        }
      }
      return true;
    }
    case value::Kind::Map: {
      if (a.map->count() != b.map->count()) {
        return false;
      }
      for (const auto& [key, mapped_value] : a.map->items()) {
        const std::string encoding = map_key_encoding(key);
        const value::Value* other_value = b.map->get(encoding);
        if (other_value == nullptr || !structural_equal(mapped_value, *other_value)) {
          return false;
        }
      }
      return true;
    }
    case value::Kind::Outcome:
    case value::Kind::Represented:
    case value::Kind::Closure:
      return false;
    case value::Kind::Ref:
      return a.ref == b.ref;
    case value::Kind::Cell:
      return a.cell == b.cell;
    case value::Kind::Process:
      return a.process == b.process;
    case value::Kind::Opaque:
      // Never observed by any pinned evidence; no defined equality.
      return false;
  }
  return false;
}

}  // namespace genia::equality
