// Native callable primitives for the E24-2..E24-7 vertical slice.
//
// map_new/map_get/map_put/map_has?/map_remove/map_count/map_items are,
// in genia-2026's real prelude (src/genia/std/prelude/map.genia), each
// a trivial single-clause, positional-parameter pass-through to a
// native primitive of the same shape (e.g.
// `map_put(map, key, value) = _map_put(map, key, value)`) -- see
// docs/design/r24/native-primitive-inventory.md's "Explicitly NOT
// native" section, which reserves prelude-sourced interpretation for
// list/map helpers in general. `err` is the same trivial-wrapper case
// (`err(..args) = _err(..args)`), except its wrapper needs a variadic
// rest parameter this slice does not otherwise implement -- see
// global_env.hpp's header comment. `_sum`/`_seq_type_error` are
// genuinely native in the real host too (no prelude wrapper at all);
// `sum`'s own trivial wrapper (`sum(xs) = _sum(xs)`) IS interpreted from
// real Genia source, like `map`/`map_acc` (see global_env.hpp), since it
// needs no variadic parameter and costs nothing extra to source
// genuinely. This project implements these wrappers natively rather
// than interpreting their prelude source text because doing so is
// behaviorally IDENTICAL (each wrapper adds no logic beyond the
// argument pass-through, or -- for `err` -- would need scope this slice
// has no other reason to add). Genuine prelude-source interpretation
// for non-trivial prelude functions (real recursion/pattern dispatch,
// e.g. `map`/`map_acc`) is real as of E24-4 -- see global_env.hpp.
//
// utf8_encode is a native Python builtin in the reference host too
// (env.set, not register_autoload) -- native here matches, not departs
// from, the reference host's own architecture.
#pragma once

#include <optional>
#include <vector>

#include "equality.hpp"
#include "float64.hpp"
#include "format.hpp"
#include "rational.hpp"
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
  if (name == "format" && args.size() == 2) {
    if (args[0].kind != value::Kind::String || args[1].kind != value::Kind::Map)
      return std::nullopt;
    auto rendered = format::render_template(args[0].text, *args[1].map);
    if (!rendered.has_value()) return std::nullopt;
    return Value::make_string(std::move(*rendered));
  }
  if (name == "float64" && args.size() == 1) {
    return float64::from_exact(args[0]);
  }
  if (name == "exact" && args.size() == 1) {
    return float64::to_exact(args[0]);
  }
  if (name == "rational" && args.size() == 2) {
    // R22 section 3: both arguments must be Integers; a zero
    // denominator is deterministic numeric misuse (this slice has no
    // diagnostic-worthy error path yet -- see rational.hpp's
    // `construct_rational` -- so it is honestly unsupported rather than
    // a fabricated diagnostic, matching `1 / 0`'s own convention).
    if (args[0].kind != value::Kind::Integer || args[1].kind != value::Kind::Integer) {
      return std::nullopt;
    }
    return rational::construct_rational(args[0].integer, args[1].integer);
  }
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
  if (name == "err" && (args.size() == 1 || args.size() == 2)) {
    // The real prelude wrapper is `err(..args) = _err(..args)` -- a
    // trivial pass-through (see global_env.hpp's header comment for why
    // this one, unlike `map`, is implemented natively rather than
    // parsed: it needs a variadic rest parameter this slice does not
    // otherwise implement).
    std::optional<Value> context;
    if (args.size() == 2) {
      context = args[1];
    }
    return Value::make_outcome_err(args[0], context);
  }
  if (name == "_sum" && args.size() == 1) {
    if (args[0].kind != value::Kind::List) {
      return std::nullopt;
    }
    bignum::Integer total = bignum::Integer::from_u64(0);
    for (const auto& item : *args[0].list_items) {
      if (item.kind != value::Kind::Integer) {
        // Real `sum` also accepts Float64; this slice has no Float64
        // yet (E24-7 scope), so a non-Integer item is unsupported
        // rather than silently coerced.
        return std::nullopt;
      }
      total = total.add(item.integer);
    }
    return Value::make_integer(total);
  }
  if (name == "_seq_type_error") {
    // Real `_seq_type_error` raises a diagnostic-worthy TypeError; this
    // slice does not yet normalize arbitrary runtime errors (E24-5
    // scope -- only the one deterministic undefined-name case is
    // wired, see evaluator.hpp), and no pinned evidence ever reaches
    // this native (it is map_acc's non-list-argument error arm), so it
    // is honestly unsupported rather than a fabricated diagnostic.
    return std::nullopt;
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
