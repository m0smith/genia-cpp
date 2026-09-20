// Projects this slice's parser AST into the `parse`-category wire shape
// genia-2026's tools/spec_runner/parse_comparator.py compares byte-exact
// against `expected.parse.ast` -- the shape hosts/python/parse_adapter.py's
// `normalize_ast` produces, not the portable Core IR (see
// docs/architecture/core-ir-portability.md: parser AST is a distinct,
// host-local layer from Core IR, but this *specific* projection is the
// documented parse-category wire contract every host's `parse` operation
// must match, per docs/design/ir.md's `{kind: Literal, value: ...}`
// table and the reference host's own normalize_ast).
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
// case being unsupported) for literals that large. No pinned E24-2
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
