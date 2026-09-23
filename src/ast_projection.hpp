// Projects this slice's parser AST into the `parse`-category wire shape
// genia-2026's tools/spec_runner/parse_comparator.py compares byte-exact
// against `expected.parse.ast` -- the shape hosts/python/parse_adapter.py's
// `normalize_ast` produces, not the portable Core IR (see
// docs/architecture/core-ir-portability.md: parser AST is a distinct,
// host-local layer from Core IR, but this *specific* projection is the
// documented parse-category wire contract every host's `parse` operation
// must match, per docs/design/ir.md's `{kind: Literal, value: ...}`
// table and the reference host's own normalize_ast).
//
// normalize_ast only special-cases ExprStmt/Number/Var/Assign/FuncDef/
// Open*/NamedPattern*/Binary; every other node type (String, Boolean,
// List, Call, ...) falls through to its bare fallback shape
// `{"kind": node_type}` -- no pinned E24-3 `parse`-category evidence
// exercises string/boolean/list/call literals, so this project mirrors
// that same fallback rather than guessing at a richer shape.
#pragma once

#include <optional>
#include <string>

#include "../third_party/nlohmann_json/json.hpp"
#include "ast.hpp"

namespace genia::ast_projection {

using json = nlohmann::json;

// A JSON number can only exactly represent this project's bignum
// literal text when it fits a native 64-bit integer; anything larger
// would silently lose precision through nlohmann::json's number type
// (which is not arbitrary-precision). Rather than emit a wrong number,
// projection fails (the caller must treat that as this specific parse
// case being unsupported) for literals that large. No pinned E24-2/E24-3
// evidence exercises this: the sole pinned `parse` case is "42".
inline std::optional<int64_t> digits_to_safe_int64(const std::string& digits) {
  if (digits.size() > 18) {  // 10^18 comfortably fits int64_t; stay well clear of overflow
    return std::nullopt;
  }
  try {
    size_t consumed = 0;
    const long long value = std::stoll(digits, &consumed);
    if (consumed != digits.size()) {
      return std::nullopt;
    }
    return static_cast<int64_t>(value);
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

inline std::optional<json> project(const ast::Node& node) {
  switch (node.kind) {
    case ast::Kind::Literal: {
      auto value = digits_to_safe_int64(node.integer_digits);
      if (!value.has_value()) {
        return std::nullopt;
      }
      return json{{"kind", "Literal"}, {"value", *value}};
    }
    case ast::Kind::DecimalLiteral: {
      // hosts/python/parse_adapter.py's real Number handler projects
      // `{"kind": "Literal", "value": node.value}` for every numeric
      // literal alike (Integer or Decimal) -- the wire shape does not
      // distinguish them, only the numeric value itself differs
      // (verified directly against that source). A JSON number can only
      // exactly represent this literal when the coefficient/exponent
      // pair fits a native double without overflow; this project never
      // emits a silently-wrong number for one that doesn't.
      try {
        size_t consumed = 0;
        const double parsed =
            std::stod(node.decimal_coefficient_digits + "e" + std::to_string(node.decimal_exponent),
                      &consumed);
        return json{{"kind", "Literal"}, {"value", parsed}};
      } catch (const std::exception&) {
        return std::nullopt;
      }
    }
    case ast::Kind::Unary:
      // hosts/python/parse_adapter.py has no special case for the real
      // Python parser's `Unary` AST node, so it falls through to the
      // bare `{"kind": node_type}` fallback -- verified directly against
      // that source, the same fallback this project already uses for
      // Lambda/MapLiteral/Spread above.
      return json{{"kind", "Unary"}};
    case ast::Kind::Quote:
      return json{{"kind", "Quote"}};
    case ast::Kind::QuasiQuote:
      return json{{"kind", "QuasiQuote"}};
    case ast::Kind::StringLiteral:
      return json{{"kind", "String"}};
    case ast::Kind::BoolLiteral:
      return json{{"kind", "Boolean"}};
    case ast::Kind::Var:
      return json{{"kind", "Var"}, {"name", node.name}};
    case ast::Kind::Binary: {
      auto left = project(*node.left);
      auto right = project(*node.right);
      if (!left.has_value() || !right.has_value()) {
        return std::nullopt;
      }
      return json{{"kind", "Binary"}, {"op", node.op}, {"left", *left}, {"right", *right}};
    }
    case ast::Kind::List:
      return json{{"kind", "List"}};
    case ast::Kind::Assign: {
      auto value = project(*node.left);
      if (!value.has_value()) {
        return std::nullopt;
      }
      return json{{"kind", "Assign"}, {"name", node.name}, {"value", *value}};
    }
    case ast::Kind::Call:
      return json{{"kind", "Call"}};
    case ast::Kind::Lambda:
      // hosts/python/parse_adapter.py's normalize_ast has no special
      // case for the real Python parser's `Lambda` AST node, so it
      // falls through to the bare `{"kind": node_type}` fallback --
      // verified directly against that source, the same fallback this
      // project already uses for String/Boolean/List/Call above.
      return json{{"kind", "Lambda"}};
    case ast::Kind::Map:
      // Same bare-fallback rule for the real `MapLiteral` AST node.
      return json{{"kind", "MapLiteral"}};
    case ast::Kind::Spread:
      // Same bare-fallback rule for the real `Spread` AST node.
      return json{{"kind", "Spread"}};
    case ast::Kind::FuncDef: {
      // normalize_ast's real FuncDef handler: `params` is always the
      // header's plain parameter names (regardless of whether the body
      // is a case-dispatch expression), and `body` recurses -- a
      // case-dispatch body has no special handler either, so it
      // projects via the same bare fallback as `CaseExpr`.
      json body;
      if (node.is_case_body) {
        body = json{{"kind", "CaseExpr"}};
      } else {
        auto projected_body = project(*node.left);
        if (!projected_body.has_value()) {
          return std::nullopt;
        }
        body = *projected_body;
      }
      return json{{"kind", "FuncDef"},
                  {"name", node.name},
                  {"params", node.header_param_names},
                  {"body", body}};
    }
    case ast::Kind::OpenFuncDef:
      // hosts/python/parse_adapter.py's real OpenFuncDef handler
      // projects only `name` and the clause count, never the clauses
      // themselves (unlike FuncDef's `body`) -- verified directly
      // against that source.
      return json{{"kind", "OpenFuncDef"},
                  {"name", node.name},
                  {"clause_count", node.case_patterns.size()}};
  }
  return std::nullopt;
}

// Projects a full program: a single-statement program projects as that
// statement's own node (matching normalize_ast's list-of-one
// unwrapping); a multi-statement program projects as a JSON array.
inline std::optional<json> project_program(const ast::Program& program) {
  if (program.empty()) {
    return std::nullopt;
  }
  if (program.size() == 1) {
    return project(program.front());
  }
  json array = json::array();
  for (const auto& node : program) {
    auto projected = project(node);
    if (!projected.has_value()) {
      return std::nullopt;
    }
    array.push_back(*projected);
  }
  return array;
}

}  // namespace genia::ast_projection
