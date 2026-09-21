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
// spec/eval/r18-*.yaml cases into wrong `ok` results). The Float64 value
// now exists for explicit conversion/rendering, while its R22 section
// 10.2 equality bridge remains a later E24-7 increment.
#pragma once

#include <optional>
#include <string>
#include <utility>

#include "bignum.hpp"
#include "value.hpp"

namespace genia::equality {

// Encodes a value as its map-key identity string: kind-tagged so
// distinct kinds (e.g. Boolean true vs Integer 1) never collide, per
// R18's "legal map keys" rule. Returns std::nullopt for a kind that is
// not (yet) a legal key at this slice -- callers must treat that as the
// whole case being unsupported, never a silent fallback to some other
// identity.
inline std::optional<std::string> map_key_encoding_checked(const value::Value& key) {
  switch (key.kind) {
    case value::Kind::Integer:
      return "I:" + key.integer.to_decimal_string();
    case value::Kind::Boolean:
      return std::string("B:") + (key.boolean ? "1" : "0");
    case value::Kind::String:
      return "S:" + key.text;
    default:
      return std::nullopt;
  }
}

// Unchecked convenience wrapper for callers that already know `key` is
// legal (e.g. re-encoding a key already stored in an OrderedMap, which
// only ever stores legal keys).
inline std::string map_key_encoding(const value::Value& key) {
  auto encoding = map_key_encoding_checked(key);
  return encoding.value_or(std::string());
}

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

inline bool structural_equal(const value::Value& a, const value::Value& b) {
  if (is_exact_family_kind(a.kind) && is_exact_family_kind(b.kind)) {
    return exact_family_equal(a, b);
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
      return a.float64 == b.float64;
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
    case value::Kind::Closure:
    case value::Kind::Opaque:
      // Never observed by any pinned evidence; no defined equality.
      return false;
  }
  return false;
}

}  // namespace genia::equality
