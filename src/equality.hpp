// R18 portable value equality for the E24-2..E24-4 vertical slice's
// supported kinds (Integer, Boolean, String, Bytes, List; Map is
// compared structurally too though no pinned evidence exercises it).
//
// docs/design/r18-portable-value-equality-contract.md: equality is ONE
// relation (==, !=), implemented as a single internal dispatch over
// Genia semantic kinds -- never falling back to host-language equality
// for a kind this dispatch does not recognize, and never letting a host
// container's own key rules decide map-key identity (see
// map_key_encoding below, which every OrderedMap operation in
// evaluator.hpp routes through). Different kinds compare unequal
// (except the Integer/Float64 bridge, which does not apply: Float64
// does not exist at this slice, and Decimal is out of scope entirely --
// E24-7's job).
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

inline bool structural_equal(const value::Value& a, const value::Value& b) {
  if (a.kind != b.kind) {
    return false;
  }
  switch (a.kind) {
    case value::Kind::Integer:
      return bignum::Integer::compare(a.integer, b.integer) == 0;
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
