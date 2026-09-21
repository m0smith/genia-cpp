// R22 sections 4-5: explicit conversion between the exact numeric family
// and the separate IEEE-754 binary64 domain.
#pragma once

#include <bit>
#include <cmath>
#include <cstdint>
#include <optional>
#include <utility>

#include "arithmetic.hpp"
#include "equality.hpp"
#include "value.hpp"

namespace genia::float64 {

struct MagnitudeOverflowError {};

inline std::optional<double> fraction_to_binary64(bignum::Integer numerator,
                                                  bignum::Integer denominator) {
  if (numerator.is_zero()) return 0.0;
  const bool negative = !numerator.is_positive_or_zero();
  numerator = numerator.absolute();

  // Contract rejection is based on exact magnitude, not on whether ordinary
  // IEEE rounding would happen to land back on the largest finite value.
  const bignum::Integer largest_finite_significand =
      bignum::Integer::from_u64((uint64_t{1} << 53) - 1);
  const bignum::Integer largest_finite = largest_finite_significand.shift_left(971);
  if (bignum::Integer::compare(numerator, denominator.mul(largest_finite)) > 0) {
    return std::nullopt;
  }

  int exponent =
      static_cast<int>(numerator.bit_length()) - static_cast<int>(denominator.bit_length());
  if (exponent >= 0) {
    if (bignum::Integer::compare(numerator, denominator.shift_left(exponent)) < 0) --exponent;
  } else if (bignum::Integer::compare(numerator.shift_left(static_cast<size_t>(-exponent)),
                                      denominator) < 0) {
    --exponent;
  }
  if (exponent > 1023) return std::nullopt;

  int scale = exponent >= -1022 ? 52 - exponent : 1074;
  bignum::Integer scaled_numerator = numerator;
  bignum::Integer scaled_denominator = denominator;
  if (scale >= 0)
    scaled_numerator = scaled_numerator.shift_left(static_cast<size_t>(scale));
  else
    scaled_denominator = scaled_denominator.shift_left(static_cast<size_t>(-scale));
  auto [quotient, remainder] =
      bignum::Integer::divmod_positive(scaled_numerator, scaled_denominator);
  auto q = quotient.to_u64();
  if (!q.has_value()) return std::nullopt;
  const int half_cmp = bignum::Integer::compare(remainder.shift_left(1), scaled_denominator);
  if (half_cmp > 0 || (half_cmp == 0 && ((*q & 1u) != 0))) ++*q;

  uint64_t magnitude_bits = 0;
  if (exponent >= -1022) {
    if (*q == (uint64_t{1} << 53)) {
      *q >>= 1;
      ++exponent;
    }
    if (exponent > 1023) return std::nullopt;
    magnitude_bits = (static_cast<uint64_t>(exponent + 1023) << 52) | (*q - (uint64_t{1} << 52));
  } else {
    if (*q >= (uint64_t{1} << 52)) {
      magnitude_bits = uint64_t{1} << 52;  // rounded to minimum normal
    } else {
      magnitude_bits = *q;
    }
  }
  const uint64_t bits = magnitude_bits | (negative ? (uint64_t{1} << 63) : 0);
  return std::bit_cast<double>(bits);
}

inline std::optional<value::Value> from_exact(const value::Value& input) {
  if (input.kind == value::Kind::Float64) return input;
  if (!equality::is_exact_family_kind(input.kind)) return std::nullopt;
  const auto [numerator, denominator] = equality::exact_family_numerator_denominator(input);
  auto converted = fraction_to_binary64(numerator, denominator);
  if (!converted.has_value()) throw MagnitudeOverflowError{};
  return value::Value::make_float64(*converted);
}

inline std::optional<value::Value> to_exact(const value::Value& input) {
  if (equality::is_exact_family_kind(input.kind)) return input;
  if (input.kind != value::Kind::Float64 || !std::isfinite(input.float64)) return std::nullopt;
  const uint64_t bits = std::bit_cast<uint64_t>(input.float64);
  const uint64_t fraction = bits & ((uint64_t{1} << 52) - 1);
  const int biased = static_cast<int>((bits >> 52) & 0x7ffu);
  if (biased == 0 && fraction == 0) {
    return value::Value::make_decimal(bignum::Integer(), 0);
  }
  uint64_t significand = biased == 0 ? fraction : ((uint64_t{1} << 52) | fraction);
  int exponent2 = biased == 0 ? -1074 : biased - 1023 - 52;
  bignum::Integer coefficient = bignum::Integer::from_u64(significand);
  int64_t exponent10 = 0;
  if (exponent2 >= 0) {
    coefficient = coefficient.shift_left(static_cast<size_t>(exponent2));
  } else {
    const bignum::Integer five = bignum::Integer::from_u64(5);
    for (int i = 0; i < -exponent2; ++i) coefficient = coefficient.mul(five);
    exponent10 = exponent2;
  }
  if ((bits >> 63) != 0) coefficient = coefficient.negate();
  auto [canonical_coefficient, canonical_exponent] =
      arithmetic::canonicalize_decimal(coefficient, exponent10);
  return value::Value::make_decimal(canonical_coefficient, canonical_exponent);
}

}  // namespace genia::float64
