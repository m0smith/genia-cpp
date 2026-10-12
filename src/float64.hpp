// R22 sections 4-5: explicit conversion between the exact numeric family
// and the separate IEEE-754 binary64 domain.
#pragma once

#include <bit>
#include <cmath>
#include <cstdint>
#include <optional>
#include <utility>

#include "arithmetic.hpp"
#include "core_ir.hpp"
#include "equality.hpp"
#include "value.hpp"

namespace genia::float64 {

// Native conversion-failure marker; caller normalizes it without exposing C++ text.
struct MagnitudeOverflowError {};
// Native Float64 division-by-either-signed-zero marker, normalized by the caller.
struct DivisionByZeroError {};
// Native Float64 remainder-by-either-signed-zero marker, normalized by the caller.
struct RemainderByZeroError {};

// Negate the boxed binary64 payload, including signed zero/non-finite values.
// Caller must supply a Float64 Value; this helper does not validate its kind.
inline value::Value negate(const value::Value& input) {
  return value::Value::make_float64(-input.float64);
}

// Compute Float64-only +,-,*,/,% using native binary64; mixed/exact operands or
// other operators return nullopt for caller dispatch/rejection. Division/remainder
// by either signed zero throw distinct native markers. Remainder adjusts fmod to
// divisor sign, including zero. Overflow/non-finite arithmetic follows binary64
// rather than throwing the exact-conversion magnitude marker; inputs are unchanged.
inline std::optional<value::Value> arithmetic(core_ir::Op op, const value::Value& left,
                                              const value::Value& right) {
  if (left.kind != value::Kind::Float64 || right.kind != value::Kind::Float64) {
    return std::nullopt;
  }
  switch (op) {
    case core_ir::Op::Plus:
      return value::Value::make_float64(left.float64 + right.float64);
    case core_ir::Op::Minus:
      return value::Value::make_float64(left.float64 - right.float64);
    case core_ir::Op::Star:
      return value::Value::make_float64(left.float64 * right.float64);
    case core_ir::Op::Slash:
      if (right.float64 == 0.0) throw DivisionByZeroError{};
      return value::Value::make_float64(left.float64 / right.float64);
    case core_ir::Op::Percent: {
      if (right.float64 == 0.0) throw RemainderByZeroError{};
      double remainder = std::fmod(left.float64, right.float64);
      if (remainder != 0.0 && std::signbit(remainder) != std::signbit(right.float64)) {
        remainder += right.float64;
      } else if (remainder == 0.0) {
        remainder = std::copysign(0.0, right.float64);
      }
      return value::Value::make_float64(remainder);
    }
    default:
      return std::nullopt;
  }
}

// Convert owned numerator/strictly-positive denominator by exact ties-to-even
// rounding, including subnormals and underflow. Exact zero returns positive zero;
// nonzero sign survives underflow. Nullopt rejects magnitude beyond largest finite
// or an unrepresentable internal quotient. Caller supplies valid finite fraction
// invariants; no denominator-zero/kind check or Python resource-limit guard exists.
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

// Preserve a Float64 Value or convert a valid exact-family Value via exact ratio
// rounding; unsupported input kinds return nullopt. Conversion absence throws
// MagnitudeOverflowError for caller normalization. Exact zero becomes positive
// zero; allocation failures propagate and Decimal exponent bounds remain host-local.
inline std::optional<value::Value> from_exact(const value::Value& input) {
  if (input.kind == value::Kind::Float64) return input;
  if (!equality::is_exact_family_kind(input.kind)) return std::nullopt;
  const auto [numerator, denominator] = equality::exact_family_numerator_denominator(input);
  auto converted = fraction_to_binary64(numerator, denominator);
  if (!converted.has_value()) throw MagnitudeOverflowError{};
  return value::Value::make_float64(*converted);
}

// Preserve exact-family Values; decode finite Float64 bits to an exact canonical
// Decimal of the represented dyadic value, normalizing either signed zero to
// Decimal zero. Non-numeric or NaN/infinity returns nullopt for caller rejection.
// This creates owned bignum storage and may allocate; it does not pass through
// approximate decimal text or claim all Python resource-limit behavior.
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
