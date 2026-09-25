// Pattern matching for the E24-4 vertical slice, mirroring
// genia-2026's src/genia/pattern_match.py `match_pattern`/
// `match_pattern_atom` exactly for the pattern kinds this slice
// implements (Bind/Wildcard/Rest/List/Map/Tuple).
//
// Two entry points, matching the reference host's own split:
//   - `match_atom`: matches one pattern against one value.
//   - `match`: matches a pattern against a full call-argument list -- a
//     Tuple pattern matches elementwise (arity must match exactly); any
//     other pattern requires exactly one argument and matches it
//     directly. This is genia-2026's real rule (a case clause or lambda
//     parameter list with N>1 positions is always an explicit Tuple
//     pattern; a bare single-position pattern only ever matches a
//     single argument).
//
// A Map pattern is intentionally "partial": it only requires the listed
// keys to be present (matching pattern-map-partial.yaml's name/verified
// behavior in pattern_match.py's IrPatMap case) -- extra keys in the
// candidate map are never rejected.
#pragma once

#include <optional>
#include <utility>
#include <vector>

#include "bignum.hpp"
#include "equality.hpp"
#include "pattern.hpp"
#include "value.hpp"

namespace genia::pattern_match {

using Bindings = std::vector<std::pair<std::string, value::Value>>;

// Merges `from` into `into`, matching genia-2026's real
// src/genia/pattern_match.py `_merge_bindings`: a name bound in both
// with UNEQUAL values (per Genia `==`) is a genuine pattern-match
// conflict, so the whole match fails (returns false) rather than one
// binding silently overwriting the other -- e.g. `([x, x]) -> true`
// against `[7, 8]` must NOT match (x can't be both 7 and 8), so that
// clause correctly falls through to the next one.
inline bool merge_bindings(Bindings& into, Bindings&& from) {
  for (auto& binding : std::move(from)) {
    for (const auto& existing : into) {
      if (existing.first == binding.first &&
          !equality::structural_equal(existing.second, binding.second)) {
        return false;
      }
    }
    into.push_back(std::move(binding));
  }
  return true;
}

inline std::optional<Bindings> match_atom(const pattern::Pattern& pattern,
                                          const value::Value& arg) {
  switch (pattern.kind) {
    case pattern::Kind::Wildcard:
      return Bindings{};
    case pattern::Kind::Bind:
      return Bindings{{pattern.name, arg}};
    case pattern::Kind::Rest:
      if (pattern.name.empty()) {
        return Bindings{};
      }
      return Bindings{{pattern.name, arg}};
    case pattern::Kind::List: {
      if (arg.kind != value::Kind::List) {
        return std::nullopt;
      }
      const auto& elements = *arg.list_items;
      std::optional<size_t> rest_index;
      for (size_t i = 0; i < pattern.items.size(); ++i) {
        if (pattern.items[i].kind == pattern::Kind::Rest) {
          rest_index = i;
          break;
        }
      }
      Bindings result;
      if (!rest_index.has_value()) {
        if (pattern.items.size() != elements.size()) {
          return std::nullopt;
        }
        for (size_t i = 0; i < pattern.items.size(); ++i) {
          auto sub = match_atom(pattern.items[i], elements[i]);
          if (!sub.has_value()) {
            return std::nullopt;
          }
          if (!merge_bindings(result, std::move(*sub))) {
            return std::nullopt;
          }
        }
        return result;
      }
      const size_t prefix_len = *rest_index;
      if (elements.size() < prefix_len) {
        return std::nullopt;
      }
      for (size_t i = 0; i < prefix_len; ++i) {
        auto sub = match_atom(pattern.items[i], elements[i]);
        if (!sub.has_value()) {
          return std::nullopt;
        }
        if (!merge_bindings(result, std::move(*sub))) {
          return std::nullopt;
        }
      }
      std::vector<value::Value> remainder(elements.begin() + static_cast<long>(prefix_len),
                                          elements.end());
      auto rest_sub =
          match_atom(pattern.items[prefix_len], value::Value::make_list(std::move(remainder)));
      if (!rest_sub.has_value()) {
        return std::nullopt;
      }
      if (!merge_bindings(result, std::move(*rest_sub))) {
        return std::nullopt;
      }
      return result;
    }
    case pattern::Kind::Map: {
      if (arg.kind != value::Kind::Map) {
        return std::nullopt;
      }
      Bindings result;
      for (const auto& [key, value_pattern] : pattern.map_items) {
        const std::string key_encoding = equality::map_key_encoding(value::Value::make_string(key));
        const value::Value* found = arg.map->get(key_encoding);
        if (found == nullptr) {
          return std::nullopt;
        }
        auto sub = match_atom(value_pattern, *found);
        if (!sub.has_value()) {
          return std::nullopt;
        }
        if (!merge_bindings(result, std::move(*sub))) {
          return std::nullopt;
        }
      }
      return result;
    }
    case pattern::Kind::Tuple:
      // A Tuple pattern only ever appears at the top level of a
      // case-clause/lambda-parameter match (see `match` below), never
      // nested inside another pattern -- no pinned evidence needs a
      // nested tuple.
      return std::nullopt;
    case pattern::Kind::Literal: {
      // "A literal pattern matches exactly when the literal and the
      // candidate are Genia-equal" (R18, mirrored from
      // pattern_match.py's IrPatLiteral case) -- reusing the one
      // canonical equality relation rather than a separate comparison,
      // so a kind mismatch (e.g. matching an Integer literal against a
      // Boolean) is correctly never a match.
      if (pattern.literal_is_string) {
        if (!equality::structural_equal(value::Value::make_string(pattern.name), arg))
          return std::nullopt;
        return Bindings{};
      }
      if (pattern.literal_is_decimal) {
        auto coefficient =
            bignum::Integer::from_unsigned_decimal(pattern.decimal_coefficient_digits);
        if (!coefficient.has_value()) return std::nullopt;
        const auto literal_value =
            value::Value::make_decimal(*coefficient, pattern.decimal_exponent);
        if (!equality::structural_equal(literal_value, arg)) return std::nullopt;
        return Bindings{};
      }
      auto literal_integer = bignum::Integer::from_unsigned_decimal(pattern.name);
      if (!literal_integer.has_value()) {
        return std::nullopt;
      }
      const value::Value literal_value = value::Value::make_integer(*literal_integer);
      if (!equality::structural_equal(literal_value, arg)) {
        return std::nullopt;
      }
      return Bindings{};
    }
    case pattern::Kind::Err: {
      if (arg.kind != value::Kind::Outcome || !arg.outcome_is_err ||
          arg.outcome_reason == nullptr || arg.outcome_context == nullptr) {
        return std::nullopt;
      }
      auto reason = match_atom(pattern.items[0], *arg.outcome_reason);
      auto context = match_atom(pattern.items[1], *arg.outcome_context);
      if (!reason.has_value() || !context.has_value()) return std::nullopt;
      if (!merge_bindings(*reason, std::move(*context))) return std::nullopt;
      return reason;
    }
  }
  return std::nullopt;
}

inline std::optional<Bindings> match(const pattern::Pattern& pattern,
                                     const std::vector<value::Value>& args) {
  if (pattern.kind == pattern::Kind::Tuple) {
    if (pattern.items.size() != args.size()) {
      return std::nullopt;
    }
    Bindings result;
    for (size_t i = 0; i < pattern.items.size(); ++i) {
      auto sub = match_atom(pattern.items[i], args[i]);
      if (!sub.has_value()) {
        return std::nullopt;
      }
      if (!merge_bindings(result, std::move(*sub))) {
        return std::nullopt;
      }
    }
    return result;
  }
  if (args.size() != 1) {
    return std::nullopt;
  }
  return match_atom(pattern, args[0]);
}

}  // namespace genia::pattern_match
