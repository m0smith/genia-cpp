// R18 portable value equality for the E24-2..E24-7 vertical slice's
// supported kinds (Integer, Decimal, Boolean, String, Bytes, List; Map
// is compared structurally too though no pinned evidence exercises it).
//
// docs/design/r18-portable-value-equality-contract.md: equality is ONE
// relation (==, !=), implemented as a single internal dispatch over
// Genia semantic kinds -- never falling back to host-language equality
// for a kind this dispatch does not recognize, and never letting a host
// container's own key rules decide map-key identity (see
// map_key_encoding below, which every OrderedMap operation in
// evaluator.hpp routes through). Different kinds compare unequal,
// except the contract's own Integer/Decimal numeric-equality bridge
// (`1 == 1.0`, "Numeric equality" table -- see `decimal_equals_integer`
// below), required as soon as Decimal literals parse at all (E24-7
// discovered this the hard way: enabling Decimal literals without this
// bridge turned two previously-honestly-`unsupported`
// spec/eval/r18-*.yaml cases into wrong `ok` results). R22's further
// Rational/Float64 cross-kind bridges (sections 10.1/10.2) remain later
// E24-7 increments -- neither value kind exists yet at this point in
// the slice.
#pragma once

#include <optional>
#include <string>

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

// R18's Integer/Decimal numeric equality bridge (contract's "Numeric
// equality" table, pre-dating R22's Integer/Decimal/Rational/Float64
// split but still the authoritative rule for this pair -- R22 section
// 14: "R18 owns equality/key architecture; R22 extends only the
// numeric cases inside that architecture"): an Integer equals a
// Decimal iff the Decimal's exact mathematical value is a whole number
// equal to the Integer. Decimal is always exact (arbitrary-precision
// base-10, never a host binary float), so this is ordinary exact-value
// comparison -- never the lossy integer-to-host-float cast the
// contract explicitly forbids. A canonical Decimal with a negative
// exponent can never be a whole number (canonicalization already
// strips every trailing base-10 zero from a nonzero coefficient, so a
// negative exponent always denotes a genuine fractional part).
inline bool decimal_equals_integer(const value::Value& decimal, const bignum::Integer& integer) {
  if (decimal.decimal_exponent < 0) {
    return false;
  }
  bignum::Integer scaled = decimal.decimal_coefficient;
  const bignum::Integer ten = bignum::Integer::from_u64(10);
  for (int64_t i = 0; i < decimal.decimal_exponent; ++i) {
    scaled = scaled.mul(ten);
  }
  return bignum::Integer::compare(scaled, integer) == 0;
}

inline bool structural_equal(const value::Value& a, const value::Value& b) {
  if (a.kind == value::Kind::Integer && b.kind == value::Kind::Decimal) {
    return decimal_equals_integer(b, a.integer);
  }
  if (a.kind == value::Kind::Decimal && b.kind == value::Kind::Integer) {
    return decimal_equals_integer(a, b.integer);
  }
  if (a.kind != b.kind) {
    return false;
  }
  switch (a.kind) {
    case value::Kind::Integer:
      return bignum::Integer::compare(a.integer, b.integer) == 0;
    case value::Kind::Decimal:
      // Same-kind case: canonical form is unique (R22 section 2), so
      // comparing coefficient/exponent directly is equivalent to
      // mathematical equality for two Decimals. Rational/Float64
      // cross-kind bridges (R22 sections 10.1/10.2) remain further
      // E24-7 increments -- neither value kind exists yet at this
      // point in the slice, so no evidence is left unhandled by their
      // absence here.
      return a.decimal_exponent == b.decimal_exponent &&
             bignum::Integer::compare(a.decimal_coefficient, b.decimal_coefficient) == 0;
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
