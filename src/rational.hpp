// R22 section 3: exact Rational construction and canonicalization.
//
// A Rational is (numerator, denominator) over arbitrary-precision
// Integers. Canonicalization: the denominator must be nonzero; both
// operands are divided by their positive gcd; the denominator is
// positive (sign carried by the numerator); a reduced denominator of 1
// canonicalizes to Integer -- so a surviving Rational value always has
// denominator > 1.
#pragma once

#include <optional>

#include "bignum.hpp"
#include "value.hpp"

namespace genia::rational {

// Constructs the `rational(numerator, denominator)` source-level
// result: `std::nullopt` for a zero denominator (R22 section 3:
// "Zero denominator is deterministic numeric misuse" -- this slice has
// no diagnostic-worthy error path yet, so the case is honestly
// unsupported rather than a fabricated diagnostic, matching
// `bignum::Integer::exact_divide`'s own zero-divisor convention).
// Returns an Integer-kind Value when the reduced denominator collapses
// to 1, a Rational-kind Value otherwise.
inline std::optional<value::Value> construct_rational(const bignum::Integer& numerator,
                                                      const bignum::Integer& denominator) {
  if (denominator.is_zero()) {
    return std::nullopt;
  }
  const bignum::Integer divisor = bignum::Integer::gcd(numerator, denominator);
  // `divisor` is zero only when both numerator and denominator are
  // zero, but denominator is already known nonzero here, so `divisor`
  // is always positive at this point.
  auto reduced_numerator = numerator.exact_divide(divisor);
  auto reduced_denominator = denominator.exact_divide(divisor);
  if (!reduced_numerator.has_value() || !reduced_denominator.has_value()) {
    return std::nullopt;  // unreachable: divisor always evenly divides both operands
  }
  // Denominator must be positive; sign is carried by the numerator.
  if (!reduced_denominator->is_positive_or_zero()) {
    reduced_numerator = reduced_numerator->negate();
    reduced_denominator = reduced_denominator->negate();
  }
  if (bignum::Integer::compare(*reduced_denominator, bignum::Integer::from_u64(1)) == 0) {
    return value::Value::make_integer(*reduced_numerator);
  }
  return value::Value::make_rational(*reduced_numerator, *reduced_denominator);
}

}  // namespace genia::rational
