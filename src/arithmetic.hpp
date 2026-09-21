// R22 sections 6-8: exact-family (Integer/Decimal/Rational) arithmetic
// for `+`, `-`, `*`, `/`, and `%`.
//
// Promotion lattice (section 6, also governing `%` per section 8):
// `Integer < Decimal < Rational` -- Decimal participation retains
// Decimal (never collapsing to Integer, even for a mathematically
// integral result); any Rational participation produces a reduced
// Rational, collapsing to Integer only when the reduced denominator is
// 1. Division (section 7) is its own table: Integer/Integer division
// that is not evenly divisible produces Rational (never Decimal, even
// when the quotient would terminate in base 10); Decimal participation
// (with no Rational operand) produces Decimal only when the exact
// quotient terminates in base 10, Rational otherwise.
#pragma once

#include <cstdint>
#include <optional>
#include <utility>

#include "bignum.hpp"
#include "equality.hpp"
#include "rational.hpp"
#include "value.hpp"

namespace genia::arithmetic {

// Strips trailing base-10 zeros from `coefficient` (raising `exponent`
// to compensate), matching R22 section 2's canonical Decimal form.
// Canonical zero is exactly coefficient 0 / exponent 0. Works for a
// negative coefficient too: `bignum::Integer::exact_divide` carries sign
// through division correctly, so the divisible-by-10 test is sign-safe.
inline std::pair<bignum::Integer, int64_t> canonicalize_decimal(bignum::Integer coefficient,
                                                                int64_t exponent) {
  if (coefficient.is_zero()) {
    return {bignum::Integer(), 0};
  }
  const bignum::Integer ten = bignum::Integer::from_u64(10);
  while (true) {
    auto divided = coefficient.exact_divide(ten);
    if (!divided.has_value()) {
      break;
    }
    coefficient = *divided;
    ++exponent;
  }
  return {coefficient, exponent};
}

// Treats an Integer operand as a Decimal with exponent 0 (its exact
// value under the coefficient*10^exponent model), letting Decimal-only
// `+`/`-`/`*` share one aligned-exponent implementation for either
// Integer or Decimal operands.
inline std::pair<bignum::Integer, int64_t> decimal_common_form(const value::Value& v) {
  if (v.kind == value::Kind::Integer) {
    return {v.integer, 0};
  }
  return {v.decimal_coefficient, v.decimal_exponent};
}

// Decimal-only (no Rational operand) `+`/`-`: aligns both operands to
// the lesser exponent, adds/subtracts the scaled coefficients, then
// canonicalizes. Always returns Decimal, never collapsing to Integer --
// R22 section 6: "Decimal participation retains Decimal ... including
// mathematically integral results".
inline value::Value decimal_add_sub(const value::Value& a, const value::Value& b, bool subtract) {
  const auto [c1, e1] = decimal_common_form(a);
  const auto [c2, e2] = decimal_common_form(b);
  const bignum::Integer ten = bignum::Integer::from_u64(10);
  const int64_t e = std::min(e1, e2);
  bignum::Integer c1_scaled = c1;
  for (int64_t i = 0; i < e1 - e; ++i) {
    c1_scaled = c1_scaled.mul(ten);
  }
  bignum::Integer c2_scaled = c2;
  for (int64_t i = 0; i < e2 - e; ++i) {
    c2_scaled = c2_scaled.mul(ten);
  }
  bignum::Integer result_coefficient =
      subtract ? c1_scaled.sub(c2_scaled) : c1_scaled.add(c2_scaled);
  const auto [canon_coefficient, canon_exponent] = canonicalize_decimal(result_coefficient, e);
  return value::Value::make_decimal(canon_coefficient, canon_exponent);
}

// Decimal-only (no Rational operand) `*`: exponents add, coefficients
// multiply, then canonicalize. Always returns Decimal.
inline value::Value decimal_mul(const value::Value& a, const value::Value& b) {
  const auto [c1, e1] = decimal_common_form(a);
  const auto [c2, e2] = decimal_common_form(b);
  const auto [canon_coefficient, canon_exponent] = canonicalize_decimal(c1.mul(c2), e1 + e2);
  return value::Value::make_decimal(canon_coefficient, canon_exponent);
}

// Reduces a fraction (`numerator`/`denominator`, `denominator` != 0) to
// lowest terms with a positive denominator, via their positive gcd --
// the same normalization `rational::construct_rational` performs,
// factored out here so `/` and `%` can inspect the reduced denominator
// (to test base-10 termination) before deciding a Decimal-vs-Rational
// result kind.
inline std::pair<bignum::Integer, bignum::Integer> reduce_fraction(bignum::Integer numerator,
                                                                   bignum::Integer denominator) {
  if (!denominator.is_positive_or_zero()) {
    numerator = numerator.negate();
    denominator = denominator.negate();
  }
  const bignum::Integer divisor = bignum::Integer::gcd(numerator, denominator);
  if (!divisor.is_zero()) {
    auto reduced_numerator = numerator.exact_divide(divisor);
    auto reduced_denominator = denominator.exact_divide(divisor);
    if (reduced_numerator.has_value() && reduced_denominator.has_value()) {
      numerator = *reduced_numerator;
      denominator = *reduced_denominator;
    }
    // unreachable otherwise: `divisor` is gcd(numerator, denominator), so
    // it always evenly divides both.
  }
  return {numerator, denominator};
}

// R22 section 7: "A reduced quotient terminates in base 10 exactly when
// its denominator has no prime factors other than 2 and 5." `denominator`
// must already be positive (as `reduce_fraction` guarantees).
inline bool denominator_terminates_in_base10(bignum::Integer denominator) {
  const bignum::Integer two = bignum::Integer::from_u64(2);
  const bignum::Integer five = bignum::Integer::from_u64(5);
  while (auto divided = denominator.exact_divide(two)) {
    denominator = *divided;
  }
  while (auto divided = denominator.exact_divide(five)) {
    denominator = *divided;
  }
  return bignum::Integer::compare(denominator, bignum::Integer::from_u64(1)) == 0;
}

// Converts an already-reduced fraction with a base-10-terminating
// denominator (2^p * 5^q) into its exact canonical Decimal form:
// multiplies numerator and denominator by 2^q * 5^p so the denominator
// becomes 10^(p+q), then canonicalizes away any excess trailing zeros.
inline value::Value decimal_from_terminating_fraction(const bignum::Integer& numerator,
                                                      bignum::Integer denominator) {
  const bignum::Integer two = bignum::Integer::from_u64(2);
  const bignum::Integer five = bignum::Integer::from_u64(5);
  int64_t twos = 0;
  int64_t fives = 0;
  while (auto divided = denominator.exact_divide(two)) {
    denominator = *divided;
    ++twos;
  }
  while (auto divided = denominator.exact_divide(five)) {
    denominator = *divided;
    ++fives;
  }
  bignum::Integer multiplier = bignum::Integer::from_u64(1);
  for (int64_t i = 0; i < fives; ++i) {
    multiplier = multiplier.mul(two);
  }
  for (int64_t i = 0; i < twos; ++i) {
    multiplier = multiplier.mul(five);
  }
  const auto [canon_coefficient, canon_exponent] =
      canonicalize_decimal(numerator.mul(multiplier), -(twos + fives));
  return value::Value::make_decimal(canon_coefficient, canon_exponent);
}

// Any-Rational-operand `+`/`-`/`*`: general exact fraction algebra
// (numerator/denominator over the Integer bignum), reduced and
// collapsed to Integer at denominator 1 by `rational::construct_rational`
// -- R22 section 6: "Rational results are reduced after each public
// operation" / "denominator-one Rational results collapse to Integer".
inline std::optional<value::Value> rational_add(const value::Value& a, const value::Value& b) {
  const auto [n1, d1] = equality::exact_family_numerator_denominator(a);
  const auto [n2, d2] = equality::exact_family_numerator_denominator(b);
  return rational::construct_rational(n1.mul(d2).add(n2.mul(d1)), d1.mul(d2));
}

inline std::optional<value::Value> rational_sub(const value::Value& a, const value::Value& b) {
  const auto [n1, d1] = equality::exact_family_numerator_denominator(a);
  const auto [n2, d2] = equality::exact_family_numerator_denominator(b);
  return rational::construct_rational(n1.mul(d2).sub(n2.mul(d1)), d1.mul(d2));
}

inline std::optional<value::Value> rational_mul(const value::Value& a, const value::Value& b) {
  const auto [n1, d1] = equality::exact_family_numerator_denominator(a);
  const auto [n2, d2] = equality::exact_family_numerator_denominator(b);
  return rational::construct_rational(n1.mul(n2), d1.mul(d2));
}

// R22 section 7: exact division. `a` and `b` must both be exact-family
// (Integer/Decimal/Rational). Returns std::nullopt for division by exact
// zero (deterministic numeric misuse -- this slice has no diagnostic-
// worthy error path yet, so it is honestly unsupported, matching every
// other zero-divisor convention already established).
inline std::optional<value::Value> exact_divide(const value::Value& a, const value::Value& b) {
  const auto [n1, d1] = equality::exact_family_numerator_denominator(a);
  const auto [n2, d2] = equality::exact_family_numerator_denominator(b);
  if (n2.is_zero()) {
    return std::nullopt;
  }
  const auto [numerator, denominator] = reduce_fraction(n1.mul(d2), n2.mul(d1));
  const bool any_rational = a.kind == value::Kind::Rational || b.kind == value::Kind::Rational;
  if (any_rational) {
    return rational::construct_rational(numerator, denominator);
  }
  const bool both_integer = a.kind == value::Kind::Integer && b.kind == value::Kind::Integer;
  if (both_integer) {
    if (bignum::Integer::compare(denominator, bignum::Integer::from_u64(1)) == 0) {
      return value::Value::make_integer(numerator);
    }
    return rational::construct_rational(numerator, denominator);
  }
  // Decimal participates, no Rational operand: Decimal when the exact
  // quotient terminates in base 10, Rational otherwise (section 7).
  if (denominator_terminates_in_base10(denominator)) {
    return decimal_from_terminating_fraction(numerator, denominator);
  }
  return rational::construct_rational(numerator, denominator);
}

// R22 section 8: exact floor-remainder, using the section 6 promotion
// rule (before Rational denominator-one collapse). `a`/`b` must both be
// exact-family. Returns std::nullopt for a zero divisor.
inline std::optional<value::Value> exact_floor_remainder(const value::Value& a,
                                                         const value::Value& b) {
  const auto [n1, d1] = equality::exact_family_numerator_denominator(a);
  const auto [n2, d2] = equality::exact_family_numerator_denominator(b);
  if (n2.is_zero()) {
    return std::nullopt;
  }
  // q = floor(left / right) = floor((n1*d2) / (n2*d1)); `floor_remainder`
  // already implements Python-style floor modulo for any operand signs
  // (verified in tests/test_bignum.cpp), so `(numerator - r) / divisor`
  // is q's exact value.
  const bignum::Integer q_numerator = n1.mul(d2);
  const bignum::Integer q_denominator = n2.mul(d1);
  auto remainder = q_numerator.floor_remainder(q_denominator);
  if (!remainder.has_value()) {
    return std::nullopt;  // unreachable: q_denominator != 0 since n2 and d1 are both nonzero
  }
  auto q_opt = q_numerator.sub(*remainder).exact_divide(q_denominator);
  if (!q_opt.has_value()) {
    return std::nullopt;  // unreachable: (q_numerator - remainder) is exactly
                          // divisible by q_denominator by floor_remainder's own contract
  }
  const bignum::Integer q = *q_opt;
  // left % right = left - q*right, as an exact fraction: (n1,d1) -
  // (q*n2, d2), common denominator d1*d2.
  const bignum::Integer numerator = n1.mul(d2).sub(q.mul(n2).mul(d1));
  const bignum::Integer denominator = d1.mul(d2);
  const auto [reduced_numerator, reduced_denominator] = reduce_fraction(numerator, denominator);
  const bool any_rational = a.kind == value::Kind::Rational || b.kind == value::Kind::Rational;
  if (any_rational) {
    return rational::construct_rational(reduced_numerator, reduced_denominator);
  }
  const bool both_integer = a.kind == value::Kind::Integer && b.kind == value::Kind::Integer;
  if (both_integer) {
    return value::Value::make_integer(reduced_numerator);  // reduced_denominator is always 1 here
  }
  // Decimal participates, no Rational operand: `d1`/`d2` are each a
  // power of 10 (or 1), so `d1*d2` is a power of 10 too, and any divisor
  // of a power of 10 has only 2 and 5 as prime factors -- the result
  // always terminates in base 10, no termination check needed.
  return decimal_from_terminating_fraction(reduced_numerator, reduced_denominator);
}

}  // namespace genia::arithmetic
