// Pattern shapes for the E24-4 vertical slice: destructuring lambda
// parameters and local (non-open-function) case/pattern dispatch.
//
// genia-2026's docs/architecture/core-ir-portability.md lists pattern
// families as their own portable node set (`IrPatBind`, `IrPatWildcard`,
// `IrPatRest`, `IrPatTuple`, `IrPatList`, `IrPatMap`, `IrPatLiteral`,
// plus others this slice does not implement: `IrPatGlob`, `IrPatSome`,
// `IrPatNone`). This slice implements exactly the subset its grammar
// produces -- Bind/Wildcard/Rest/List/Map/Tuple/Literal -- matching the
// names in that contract. Literal is deliberately narrow (integer
// digits only, matching genia-2026's real IrPatLiteral value shape for
// this case but not its full generality: no string/bool/nil literal
// patterns, since no pinned evidence needs them -- e.g. R20's
// `open gcd(a, 0) = a` needs only an integer-literal clause pattern).
//
// Unlike expression nodes (which have a real, separately-shaped parser
// AST and Core IR -- see ast.hpp/core_ir.hpp/lowering.hpp), no pinned
// E24-4 evidence exercises `parse` or `lower` category conformance for
// pattern-bearing constructs, so this one struct is shared directly by
// both the parser and the evaluator rather than duplicating it across a
// pattern-specific AST layer and a pattern-specific Core IR layer.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace genia::pattern {

enum class Kind : std::uint8_t { Bind, Wildcard, Rest, List, Map, Tuple, Literal, Err };

struct Pattern {
  Kind kind = Kind::Wildcard;

  // Bind, Rest: the name to bind (Rest's name may be empty, meaning the
  // spread remainder is discarded rather than bound). Literal: the
  // canonical unsigned decimal digit text to match against (see
  // ast.hpp's Literal node -- sign is never part of a literal pattern,
  // matching this slice's expression-literal grammar).
  std::string name;
  bool literal_is_decimal = false;
  bool literal_is_string = false;
  std::string decimal_coefficient_digits;
  int64_t decimal_exponent = 0;

  // List: element patterns, at most one of which may be Kind::Rest (and
  // only as the final item, matching genia-2026's "..rest must be the
  // final item in a list pattern" rule). Tuple: one pattern per
  // top-level case-clause/lambda-parameter position.
  std::vector<Pattern> items;

  // Map: key -> value-pattern pairs. A bare `{name}` shorthand key
  // lowers to `{name, Bind(name)}` at parse time (see parser.hpp),
  // matching genia-2026's real map-pattern shorthand rule.
  std::vector<std::pair<std::string, Pattern>> map_items;

  static Pattern bind(std::string bound_name) {
    Pattern p;
    p.kind = Kind::Bind;
    p.name = std::move(bound_name);
    return p;
  }

  static Pattern wildcard() {
    Pattern p;
    p.kind = Kind::Wildcard;
    return p;
  }

  static Pattern rest(std::string bound_name) {
    Pattern p;
    p.kind = Kind::Rest;
    p.name = std::move(bound_name);
    return p;
  }

  static Pattern integer_literal(std::string digits) {
    Pattern p;
    p.kind = Kind::Literal;
    p.name = std::move(digits);
    return p;
  }

  static Pattern decimal_literal(std::string coefficient_digits, int64_t exponent) {
    Pattern p;
    p.kind = Kind::Literal;
    p.literal_is_decimal = true;
    p.decimal_coefficient_digits = std::move(coefficient_digits);
    p.decimal_exponent = exponent;
    return p;
  }

  static Pattern string_literal(std::string text) {
    Pattern p;
    p.kind = Kind::Literal;
    p.literal_is_string = true;
    p.name = std::move(text);
    return p;
  }

  static Pattern list(std::vector<Pattern> element_patterns) {
    Pattern p;
    p.kind = Kind::List;
    p.items = std::move(element_patterns);
    return p;
  }

  static Pattern tuple(std::vector<Pattern> position_patterns) {
    Pattern p;
    p.kind = Kind::Tuple;
    p.items = std::move(position_patterns);
    return p;
  }

  static Pattern map(std::vector<std::pair<std::string, Pattern>> key_patterns) {
    Pattern p;
    p.kind = Kind::Map;
    p.map_items = std::move(key_patterns);
    return p;
  }

  static Pattern err(Pattern reason, Pattern context) {
    Pattern p;
    p.kind = Kind::Err;
    p.items.push_back(std::move(reason));
    p.items.push_back(std::move(context));
    return p;
  }
};

}  // namespace genia::pattern
