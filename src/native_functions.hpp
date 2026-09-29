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

#include "cell.hpp"
#include "equality.hpp"
#include "float64.hpp"
#include "format.hpp"
#include "json.hpp"
#include "process.hpp"
#include "rational.hpp"
#include "ref.hpp"
#include "utf8.hpp"
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
  if (name == "none" && args.size() == 1) return Value::make_outcome_none(args[0]);
  if (name == "cell" && args.size() == 1) return Value::make_cell(value::Cell::create(args[0]));
  if (name == "cell_with_state" && args.size() == 1 && args[0].kind == value::Kind::Ref)
    return Value::make_cell(value::Cell::create_with_state(args[0].ref));
  if ((name == "cell_get" || name == "cell_state") && args.size() == 1 &&
      args[0].kind == value::Kind::Cell)
    return args[0].cell->get();
  if (name == "cell_status" && args.size() == 1 && args[0].kind == value::Kind::Cell) {
    const auto status = args[0].cell->status();
    return Value::make_string(status == value::Cell::Status::Ready     ? "ready"
                              : status == value::Cell::Status::Stopped ? "stopped"
                                                                       : "failed");
  }
  if ((name == "cell_alive?" || name == "cell_failed?") && args.size() == 1 &&
      args[0].kind == value::Kind::Cell) {
    const auto status = args[0].cell->status();
    return Value::make_boolean(name == "cell_alive?" ? status == value::Cell::Status::Ready
                                                     : status == value::Cell::Status::Failed);
  }
  if (name == "cell_error" && args.size() == 1 && args[0].kind == value::Kind::Cell) {
    auto error = args[0].cell->error();
    if (error.has_value()) return Value::make_outcome_some(Value::make_string(*error));
    return Value::make_outcome_none(Value::make_string("nil"));
  }
  if (name == "cell_stop" && args.size() == 1 && args[0].kind == value::Kind::Cell) {
    args[0].cell->stop();
    return args[0];
  }
  if (name == "restart_cell" && args.size() == 2 && args[0].kind == value::Kind::Cell) {
    args[0].cell->restart(args[1]);
    return args[0];
  }
  if (name == "_r25_await_idle" && args.empty() && value::g_r25_fixture_enabled) {
    for (int pass = 0; pass < 2; ++pass) {
      value::Cell::await_all_idle();
      value::Process::await_all_idle();
    }
    return Value::make_boolean(true);
  }
  if ((name == "process_alive?" || name == "process_failed?") && args.size() == 1 &&
      args[0].kind == value::Kind::Process)
    return Value::make_boolean(name == "process_alive?" ? args[0].process->alive()
                                                        : args[0].process->failed());
  if (name == "process_error" && args.size() == 1 && args[0].kind == value::Kind::Process) {
    auto error = args[0].process->error();
    if (error.has_value()) return Value::make_outcome_some(Value::make_string(*error));
    return Value::make_outcome_none(Value::make_string("nil"));
  }
  if (name == "append" && args.size() == 2 && args[0].kind == value::Kind::List &&
      args[1].kind == value::Kind::List) {
    std::vector<Value> items = *args[0].list_items;
    items.insert(items.end(), args[1].list_items->begin(), args[1].list_items->end());
    return Value::make_list(std::move(items));
  }
  if ((name == "some?" || name == "is_some?") && args.size() == 1)
    return Value::make_boolean(args[0].kind == value::Kind::Outcome && !args[0].outcome_is_err &&
                               !args[0].outcome_is_none);
  if (name == "ref" && args.empty()) {
    return Value::make_ref(std::make_shared<value::Ref>());
  }
  if (name == "ref" && args.size() == 1) {
    return Value::make_ref(std::make_shared<value::Ref>(args[0]));
  }
  if (name == "ref_get" && args.size() == 1 && args[0].kind == value::Kind::Ref) {
    return args[0].ref->get();
  }
  if (name == "ref_set" && args.size() == 2 && args[0].kind == value::Kind::Ref) {
    return args[0].ref->set(args[1]);
  }
  if (name == "ref_is_set" && args.size() == 1 && args[0].kind == value::Kind::Ref) {
    return Value::make_boolean(args[0].ref->is_set());
  }
  if (name == "json_encode" && args.size() == 1) {
    auto encoded = strict_json::encode_value(args[0]);
    if (encoded.value.has_value()) {
      return Value::make_outcome_some(*encoded.value,
                                      strict_json::success_context("encode", "encoded"));
    }
    return Value::make_outcome_err(Value::make_string(strict_json::reason(encoded.failure)),
                                   strict_json::build_context("encode", "error", encoded.failure));
  }
  // Argument-kind validation (String or Bytes) happens in evaluator.hpp
  // before this native is ever reached -- a non-String/Bytes argument is
  // a `StatefulRuntimeError` (contract: programmer misuse, not a
  // recoverable Outcome), matching `error-json-decode-input-type.yaml`.
  if (name == "json_decode" && args.size() == 1 &&
      (args[0].kind == value::Kind::String || args[0].kind == value::Kind::Bytes)) {
    std::string text;
    if (args[0].kind == value::Kind::Bytes) {
      if (!utf8::is_well_formed(args[0].text)) {
        strict_json::Failure failure{strict_json::Error::InvalidJson};
        // No dedicated `invalid_json_utf8` Error enumerator is needed
        // yet -- no pinned evidence constructs malformed UTF-8 bytes for
        // this path (contract section 6), so this is an honest,
        // conservative fallback rather than a fabricated distinct code
        // path.
        return Value::make_outcome_err(Value::make_string("invalid_json_utf8"),
                                       strict_json::build_context("decode", "error", failure));
      }
      text = args[0].text;
    } else {
      text = args[0].text;
    }
    auto decoded = strict_json::decode_document(text);
    if (decoded.value.has_value()) {
      return Value::make_outcome_some(Value::make_represented("json", *decoded.value),
                                      strict_json::success_context("decode", "decoded"));
    }
    return Value::make_outcome_err(Value::make_string(strict_json::reason(decoded.failure)),
                                   strict_json::build_context("decode", "error", decoded.failure));
  }
  if (name == "represent" && args.size() == 2 && args[0].kind == value::Kind::String) {
    return Value::make_represented(args[0].text, args[1]);
  }
  if (name == "nth" && args.size() == 2 && args[0].kind == value::Kind::Integer &&
      args[1].kind == value::Kind::List) {
    // src/genia/std/prelude/list.genia's `nth(n, xs)`: `some(value)` for
    // an in-range zero-based index, else
    // `none("index-out-of-bounds", {index, length})`.
    static const bignum::Integer zero = bignum::Integer::from_u64(0);
    const auto& items = *args[1].list_items;
    const bool negative = bignum::Integer::compare(args[0].integer, zero) < 0;
    bool in_range = false;
    size_t index = 0;
    if (!negative) {
      auto index_u64 = args[0].integer.to_u64();
      if (index_u64.has_value() && *index_u64 < items.size()) {
        in_range = true;
        index = static_cast<size_t>(*index_u64);
      }
    }
    if (in_range) return Value::make_outcome_some(items[index]);
    auto context = std::make_shared<value::OrderedMap>();
    auto put = [&](const std::string& key, Value val) {
      auto key_value = Value::make_string(key);
      context->put(equality::map_key_encoding(key_value), key_value, std::move(val));
    };
    put("index", args[0]);
    put("length", Value::make_integer(bignum::Integer::from_u64(items.size())));
    return Value::make_outcome_none(Value::make_string("index-out-of-bounds"),
                                    Value::make_map(context));
  }
  if (name == "none?" && args.size() == 1) {
    return Value::make_boolean(args[0].kind == value::Kind::Outcome && !args[0].outcome_is_err &&
                               args[0].outcome_is_none);
  }
  if (name == "get" && args.size() == 2 && args[1].kind == value::Kind::Map) {
    // src/genia/std/prelude/option.genia's `get(key, target)`: `some(value)`
    // for a present legal key, else `none("missing-key", {key})`.
    auto key_encoding = equality::map_key_encoding_checked(args[0]);
    if (!key_encoding.has_value()) return std::nullopt;
    const Value* found = args[1].map->get(*key_encoding);
    if (found != nullptr) return Value::make_outcome_some(*found);
    auto context = std::make_shared<value::OrderedMap>();
    auto key_value = Value::make_string("key");
    context->put(equality::map_key_encoding(key_value), key_value, args[0]);
    return Value::make_outcome_none(Value::make_string("missing-key"), Value::make_map(context));
  }
  if (name == "unwrap_or" && args.size() == 2 && args[1].kind == value::Kind::Outcome &&
      !args[1].outcome_is_err && args[1].outcome_value != nullptr) {
    return *args[1].outcome_value;
  }
  if (name == "representation_match" && args.size() == 2 && args[0].kind == value::Kind::String) {
    if (args[1].kind == value::Kind::Represented && args[0].text == args[1].represented_facet) {
      return Value::make_outcome_some(*args[1].represented_value);
    }
    // genia-2026's real `representation_match` returns
    // `none("representation-mismatch")` for any non-matching value
    // (unrepresented, or represented under a different facet) --
    // src/genia/builtins.py's `representation_match_fn` -- rather than
    // being honestly unsupported outside the exact-match case.
    return Value::make_outcome_none(Value::make_string("representation-mismatch"));
  }
  if (name == "display" && args.size() == 1) {
    if (args[0].kind == value::Kind::String) return args[0];
    auto rendered = render::display(args[0]);
    if (!rendered.has_value()) return std::nullopt;
    return Value::make_string(*rendered);
  }
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
  if (name == "utf8_decode" && args.size() == 1) {
    // R26-2 bytes_utf8 contract (genia-2026 issue #1024,
    // docs/design/r26-cpp-data-bridge-contract.md section 2): well-formed
    // input only. A non-Bytes argument, or genuinely malformed UTF-8
    // bytes, is honestly `unsupported` here -- this slice does not yet
    // normalize either into a diagnostic-worthy error (matching
    // `utf8_encode`'s own existing non-String-argument convention just
    // above), and no shared evidence pins the malformed-input error path
    // (contract section 6: Genia source cannot construct arbitrary
    // invalid UTF-8 bytes today, so the error path cannot be honestly
    // evidenced yet either).
    if (args[0].kind != value::Kind::Bytes) {
      return std::nullopt;
    }
    if (!utf8::is_well_formed(args[0].text)) {
      return std::nullopt;
    }
    return Value::make_string(args[0].text);
  }
  return std::nullopt;
}

}  // namespace genia::native_functions
