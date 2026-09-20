// Native callable primitives for the E24-3 vertical slice.
//
// map_new/map_get/map_put/map_has?/map_remove/map_count/map_items are,
// in genia-2026's real prelude (src/genia/std/prelude/map.genia), each
// a trivial single-clause, positional-parameter pass-through to a
// native primitive of the same shape (e.g.
// `map_put(map, key, value) = _map_put(map, key, value)`) -- see
// docs/design/r24/native-primitive-inventory.md's "Explicitly NOT
// native" section, which reserves prelude-sourced interpretation for
// list/map helpers in general. This slice implements these seven
// specific wrappers as native C++ callables directly rather than
// interpreting their prelude source text, because doing so is
// behaviorally IDENTICAL (the wrapper adds no logic beyond the
// argument pass-through) and this slice does not yet implement
// user-level function *definitions* at all (no pinned E24-2/E24-3
// evidence needs one -- every case only *calls* existing native or
// prelude functions). Genuine prelude-source interpretation, needed for
// non-trivial prelude functions (map_keys/map_values and their
// map/map_acc/pattern-dispatch/recursion dependency chain -- see
// m0smith/genia-2026#968), remains E24-4+ scope, once real function
// *definitions* exist.
//
// utf8_encode is a native Python builtin in the reference host too
// (env.set, not register_autoload) -- native here matches, not departs
// from, the reference host's own architecture.
#pragma once

#include <optional>
#include <vector>

#include "equality.hpp"
#include "value.hpp"

namespace genia::native_functions {

using value::Value;

// Returns std::nullopt when `name`/arity is not one of this slice's
// native callables, or when a call's arguments are shaped in a way
// this slice cannot honestly evaluate (e.g. a map key of a kind that
// is not yet a legal key -- see equality.hpp's map_key_encoding_checked).
// Callers must treat std::nullopt as "this case is unsupported", never
// attempt a fallback value.
inline std::optional<Value> call(const std::string& name, const std::vector<Value>& args) {
  if (name == "map_new" && args.empty()) {
    return Value::make_map(std::make_shared<value::OrderedMap>());
  }
  if (name == "map_put" && args.size() == 3) {
    if (args[0].kind != value::Kind::Map) {
      return std::nullopt;
    }
    auto key_encoding = equality::map_key_encoding_checked(args[1]);
    if (!key_encoding.has_value()) {
      return std::nullopt;
    }
    // map_put returns a NEW map (source unchanged) per its @doc
    // contract; this slice's OrderedMap has no structural sharing yet,
    // so a full copy is the honest (if not optimized) way to preserve
    // that "returned unchanged" guarantee for the original binding.
    auto new_map = std::make_shared<value::OrderedMap>(*args[0].map);
    new_map->put(*key_encoding, args[1], args[2]);
    return Value::make_map(new_map);
  }
  if (name == "map_get" && args.size() == 2) {
    if (args[0].kind != value::Kind::Map) {
      return std::nullopt;
    }
    auto key_encoding = equality::map_key_encoding_checked(args[1]);
    if (!key_encoding.has_value()) {
      return std::nullopt;
    }
    const Value* found = args[0].map->get(*key_encoding);
    if (found == nullptr) {
      // The real contract returns none("missing-key", {key: key}) here
      // (an Option value). This slice has no Option support yet (E24-4
      // scope), so a missing key must be unsupported rather than a
      // fabricated result -- no pinned evidence exercises a missing
      // key.
      return std::nullopt;
    }
    return *found;
  }
  if (name == "map_has?" && args.size() == 2) {
    if (args[0].kind != value::Kind::Map) {
      return std::nullopt;
    }
    auto key_encoding = equality::map_key_encoding_checked(args[1]);
    if (!key_encoding.has_value()) {
      return std::nullopt;
    }
    return Value::make_boolean(args[0].map->has(*key_encoding));
  }
  if (name == "map_remove" && args.size() == 2) {
    if (args[0].kind != value::Kind::Map) {
      return std::nullopt;
    }
    auto key_encoding = equality::map_key_encoding_checked(args[1]);
    if (!key_encoding.has_value()) {
      return std::nullopt;
    }
    auto new_map = std::make_shared<value::OrderedMap>(*args[0].map);
    new_map->remove(*key_encoding);
    return Value::make_map(new_map);
  }
  if (name == "map_count" && args.size() == 1) {
    if (args[0].kind != value::Kind::Map) {
      return std::nullopt;
    }
    return Value::make_integer(bignum::Integer::from_u64(args[0].map->count()));
  }
  if (name == "map_items" && args.size() == 1) {
    if (args[0].kind != value::Kind::Map) {
      return std::nullopt;
    }
    std::vector<Value> pairs;
    pairs.reserve(args[0].map->count());
    for (const auto& [key, mapped_value] : args[0].map->items()) {
      pairs.push_back(Value::make_list({key, mapped_value}));
    }
    return Value::make_list(std::move(pairs));
  }
  if (name == "utf8_encode" && args.size() == 1) {
    if (args[0].kind != value::Kind::String) {
      return std::nullopt;
    }
    // The reference host's utf8_encode encodes a Unicode string into
    // its UTF-8 byte sequence. This slice already stores string
    // literals as their raw UTF-8 source bytes (no escape processing
    // beyond plain text -- see parser.hpp), so encoding is exactly
    // that stored byte content reinterpreted as Bytes.
    return Value::make_bytes(args[0].text);
  }
  return std::nullopt;
}

}  // namespace genia::native_functions
