// Runtime values for the E24-2..E24-7 vertical slice.
//
// Kinds: exact Integer, exact Decimal (E24-7: R22 section 2's
// coefficient/exponent value; literal construction and unary negation
// only at this point in the slice -- Decimal arithmetic/comparison and
// Rational/Float64 remain further E24-7 increments), Boolean, String,
// Bytes (from utf8_encode), List, Map (the native in-house
// insertion-ordered map), Outcome (E24-4: the `err(reason)`/
// `err(reason, context)` recoverable-failure constructor only --
// `some`/`none` remain unimplemented, no pinned evidence needs them),
// Closure (E24-4: a lambda or named-function value -- either a single
// ordinary body evaluated after positional-pattern parameter binding,
// or a local case/pattern-dispatch body tried clause by clause), and an
// opaque placeholder for a known-but-not-yet-callable global binding
// (see global_env.hpp). Float64 is the explicit boxed binary64 domain;
// its arithmetic/comparison integration remains later E24-7 work. This
// is deliberately not a general Genia value representation yet.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "bignum.hpp"
#include "pattern.hpp"

namespace genia::core_ir {
struct Node;
}  // namespace genia::core_ir

namespace genia::evaluator {
class Environment;
}  // namespace genia::evaluator

namespace genia::value {

enum class Kind : std::uint8_t {
  Integer,
  Decimal,
  Rational,
  Float64,
  Boolean,
  String,
  Bytes,
  List,
  Map,
  Outcome,
  Represented,
  Closure,
  Ref,
  Cell,
  Opaque
};

class OrderedMap;
class Ref;
class Cell;
struct Value;

// One clause of a local case/pattern-dispatch function body (E24-4's
// `pattern_case_dispatch` category): tried in source order against the
// call's argument list (see pattern_match.hpp's `match`); the first
// clause whose pattern matches supplies the result expression.
struct CaseClause {
  pattern::Pattern pattern;
  std::shared_ptr<core_ir::Node> result;
};

// A lambda or named-function value. Exactly one of `case_clauses` or
// `params` is meaningful for a given closure (never both): a
// case-bodied function (e.g. `map_acc`) carries `case_clauses` and
// binds nothing positionally beforehand; an ordinary function/lambda
// (e.g. `inc(x) = x + 1`, or `([a, b]) -> a + b`) carries one
// positional pattern per parameter and a single `body` expression.
struct Closure {
  std::vector<pattern::Pattern>
      params;  // one pattern per positional parameter; empty when case_clauses is used
  std::vector<CaseClause> case_clauses;  // populated only for a case-dispatch body
  std::shared_ptr<core_ir::Node> body;   // populated only when case_clauses is empty
  std::shared_ptr<evaluator::Environment> captured_env;
  std::string name;  // the bound name for a named function; empty for an anonymous lambda
};

struct Value {
  Kind kind = Kind::Opaque;
  bignum::Integer integer;  // valid when kind == Integer
  // valid when kind == Decimal (R22 section 2): value is
  // decimal_coefficient * 10^decimal_exponent, canonicalized (zero is
  // exactly coefficient 0/exponent 0; a nonzero coefficient's magnitude
  // has no trailing base-10 zeros -- see parser.hpp's
  // `canonicalize_decimal_literal`).
  bignum::Integer decimal_coefficient;
  int64_t decimal_exponent = 0;
  // valid when kind == Rational (R22 section 3): a *surviving* Rational
  // (one that was not collapsed to Integer at construction time, see
  // rational.hpp's `construct_rational`) always has
  // rational_denominator > 1, sign carried by rational_numerator.
  bignum::Integer rational_numerator;
  bignum::Integer rational_denominator;
  double float64 = 0.0;  // valid when kind == Float64; preserves the exact binary64 bits
  bool boolean = false;  // valid when kind == Boolean
  std::string text;      // valid when kind == String or Bytes (raw UTF-8/byte content)
  std::shared_ptr<std::vector<Value>> list_items;  // valid when kind == List
  std::shared_ptr<OrderedMap> map;                 // valid when kind == Map

  // Outcome (`err(reason)` / `err(reason, context)`): `outcome_context`
  // is nullptr when the 1-argument form was used.
  std::shared_ptr<Value> outcome_reason;
  std::shared_ptr<Value> outcome_context;
  std::shared_ptr<Value> outcome_value;
  bool outcome_is_err = true;
  bool outcome_is_none = false;

  std::string represented_facet;
  std::shared_ptr<Value> represented_value;

  std::shared_ptr<Closure> closure;  // valid when kind == Closure
  std::shared_ptr<Ref> ref;          // valid when kind == Ref
  std::shared_ptr<Cell> cell;        // valid when kind == Cell

  static Value make_integer(bignum::Integer v) {
    Value value;
    value.kind = Kind::Integer;
    value.integer = std::move(v);
    return value;
  }

  // `coefficient`/`exponent` must already be canonical (R22 section 2)
  // -- this constructor does not re-canonicalize, matching how
  // `make_integer` trusts its caller. The parser's
  // `canonicalize_decimal_literal` (parser.hpp) is the only producer of
  // canonical coefficient/exponent pairs at this slice's scope; negating
  // one (unary minus) preserves canonical form trivially.
  static Value make_decimal(bignum::Integer coefficient, int64_t exponent) {
    Value value;
    value.kind = Kind::Decimal;
    value.decimal_coefficient = std::move(coefficient);
    value.decimal_exponent = exponent;
    return value;
  }

  // `numerator`/`denominator` must already be canonical (R22 section 3:
  // reduced by their positive gcd, denominator positive, denominator
  // never 1 for a surviving Rational) -- this constructor does not
  // re-canonicalize, matching `make_decimal`. `rational.hpp`'s
  // `construct_rational` is the only producer of canonical pairs.
  static Value make_rational(bignum::Integer numerator, bignum::Integer denominator) {
    Value value;
    value.kind = Kind::Rational;
    value.rational_numerator = std::move(numerator);
    value.rational_denominator = std::move(denominator);
    return value;
  }

  static Value make_float64(double v) {
    Value value;
    value.kind = Kind::Float64;
    value.float64 = v;
    return value;
  }

  static Value make_boolean(bool v) {
    Value value;
    value.kind = Kind::Boolean;
    value.boolean = v;
    return value;
  }

  static Value make_string(std::string v) {
    Value value;
    value.kind = Kind::String;
    value.text = std::move(v);
    return value;
  }

  static Value make_bytes(std::string v) {
    Value value;
    value.kind = Kind::Bytes;
    value.text = std::move(v);
    return value;
  }

  static Value make_list(std::vector<Value> items) {
    Value value;
    value.kind = Kind::List;
    value.list_items = std::make_shared<std::vector<Value>>(std::move(items));
    return value;
  }

  static Value make_map(std::shared_ptr<OrderedMap> m) {
    Value value;
    value.kind = Kind::Map;
    value.map = std::move(m);
    return value;
  }

  static Value make_outcome_err(Value reason, std::optional<Value> context) {
    Value value;
    value.kind = Kind::Outcome;
    value.outcome_reason = std::make_shared<Value>(std::move(reason));
    if (context.has_value()) {
      value.outcome_context = std::make_shared<Value>(std::move(*context));
    }
    return value;
  }

  static Value make_outcome_some(Value inner) {
    Value value;
    value.kind = Kind::Outcome;
    value.outcome_is_err = false;
    value.outcome_value = std::make_shared<Value>(std::move(inner));
    return value;
  }

  static Value make_outcome_none(Value reason, Value context) {
    Value value;
    value.kind = Kind::Outcome;
    value.outcome_is_err = false;
    value.outcome_is_none = true;
    value.outcome_reason = std::make_shared<Value>(std::move(reason));
    value.outcome_context = std::make_shared<Value>(std::move(context));
    return value;
  }

  static Value make_represented(std::string facet, Value inner) {
    Value value;
    value.kind = Kind::Represented;
    value.represented_facet = std::move(facet);
    value.represented_value = std::make_shared<Value>(std::move(inner));
    return value;
  }

  static Value make_closure(std::shared_ptr<Closure> c) {
    Value value;
    value.kind = Kind::Closure;
    value.closure = std::move(c);
    return value;
  }

  static Value make_ref(std::shared_ptr<Ref> r) {
    Value value;
    value.kind = Kind::Ref;
    value.ref = std::move(r);
    return value;
  }

  static Value make_cell(std::shared_ptr<Cell> c) {
    Value value;
    value.kind = Kind::Cell;
    value.cell = std::move(c);
    return value;
  }

  static Value make_opaque() {
    Value value;
    value.kind = Kind::Opaque;
    return value;
  }
};

// The native in-house insertion-ordered map primitive (per
// docs/design/r24/dependency-toolchain-policy.md: a vector of pairs
// plus a hash index, never std::map/std::unordered_map, which do not
// preserve insertion order). Key legality/equivalence follows R18
// exactly (docs/design/r18-portable-value-equality-contract.md "Legal
// map keys and key equivalence"): key identity is exactly Genia `==`,
// so lookup never falls back to a host container's own key rules --
// see equality.hpp's `map_key_encoding`, which this type uses
// exclusively for its hash index.
class OrderedMap {
 public:
  // Returns the index of an existing entry for `key_encoding`, or
  // appends a fresh entry and returns its index.
  size_t put(const std::string& key_encoding, Value key, Value mapped_value) {
    auto it = index_.find(key_encoding);
    if (it != index_.end()) {
      entries_[it->second].first = std::move(key);
      entries_[it->second].second = std::move(mapped_value);
      return it->second;
    }
    const size_t new_index = entries_.size();
    entries_.emplace_back(std::move(key), std::move(mapped_value));
    index_.emplace(key_encoding, new_index);
    return new_index;
  }

  bool has(const std::string& key_encoding) const { return index_.count(key_encoding) > 0; }

  const Value* get(const std::string& key_encoding) const {
    auto it = index_.find(key_encoding);
    if (it == index_.end()) {
      return nullptr;
    }
    return &entries_[it->second].second;
  }

  void remove(const std::string& key_encoding) {
    auto it = index_.find(key_encoding);
    if (it == index_.end()) {
      return;
    }
    const size_t removed_index = it->second;
    entries_.erase(entries_.begin() + static_cast<long>(removed_index));
    index_.erase(it);
    for (auto& pair_entry : index_) {
      if (pair_entry.second > removed_index) {
        --pair_entry.second;
      }
    }
  }

  size_t count() const { return entries_.size(); }

  const std::vector<std::pair<Value, Value>>& items() const { return entries_; }

 private:
  std::vector<std::pair<Value, Value>> entries_;
  std::unordered_map<std::string, size_t> index_;
};

}  // namespace genia::value
