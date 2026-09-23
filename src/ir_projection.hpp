// Projects this slice's Core IR into the portable wire shape
// hosts/python/ir_normalize.py's `_normalize_ir_node`/`_normalize_pattern`
// produce (the `lower` operation's `result.ir`). No `ir`/`lower`-category
// bootstrap case is pinned yet, but this is built honestly against the
// documented contract now rather than deferred, per AGENTS.md's "no
// parser-to-runtime shortcuts" rule -- the `lower` operation is real, not
// a stub, even before evidence exercises it.
//
// `project`/`project_program` return std::optional: a node this
// projection cannot honestly represent (e.g. this slice's own
// `Kind::Spread` reached outside a list, which never happens in
// practice -- see evaluator.hpp) fails the whole projection rather than
// silently emitting a JSON null in its place, matching
// ast_projection.hpp's already-safer optional-threading pattern.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "../third_party/nlohmann_json/json.hpp"
#include "core_ir.hpp"
#include "pattern.hpp"

namespace genia::ir_projection {

using json = nlohmann::json;

inline std::optional<json> project(const core_ir::Node& node);

// Mirrors src/genia/lowering.py's `_lambda_pattern_is_simple_parameter_shape`:
// a lambda whose every positional parameter is a plain Bind (never a
// destructuring List/Map/Wildcard/Rest pattern) projects with `params`
// as plain names and no `pattern` field; anything else projects `params`
// as empty and carries the full pattern via `pattern`.
inline bool is_simple_bind_shape(const std::vector<pattern::Pattern>& params) {
  for (const auto& p : params) {
    if (p.kind != pattern::Kind::Bind) {
      return false;
    }
  }
  return true;
}

// A JSON number can only exactly represent this project's bignum
// literal text when it fits a native 64-bit integer (see
// ast_projection.hpp's `digits_to_safe_int64`, duplicated narrowly here
// rather than introducing a cross-dependency between the two projection
// headers for one small helper).
inline std::optional<int64_t> pattern_digits_to_safe_int64(const std::string& digits) {
  if (digits.size() > 18) {
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

inline std::optional<json> project_pattern(const pattern::Pattern& pattern) {
  switch (pattern.kind) {
    case pattern::Kind::Literal: {
      // genia-2026's real IrPatLiteral carries the raw value (unlike
      // ordinary IrLiteral's R21 tagged numeric payload -- case-pattern
      // numeric literals are explicitly unaffected by that ticket, see
      // pattern.hpp's header comment).
      auto value = pattern_digits_to_safe_int64(pattern.name);
      if (!value.has_value()) {
        return std::nullopt;
      }
      return json{{"node", "IrPatLiteral"}, {"value", *value}};
    }
    case pattern::Kind::Bind:
      return json{{"node", "IrPatBind"}, {"name", pattern.name}};
    case pattern::Kind::Wildcard:
      return json{{"node", "IrPatWildcard"}};
    case pattern::Kind::Rest:
      return json{{"node", "IrPatRest"}, {"name", pattern.name}};
    case pattern::Kind::Tuple: {
      json items = json::array();
      for (const auto& item : pattern.items) {
        auto projected_item = project_pattern(item);
        if (!projected_item.has_value()) {
          return std::nullopt;
        }
        items.push_back(*projected_item);
      }
      return json{{"node", "IrPatTuple"}, {"items", items}};
    }
    case pattern::Kind::List: {
      json items = json::array();
      for (const auto& item : pattern.items) {
        auto projected_item = project_pattern(item);
        if (!projected_item.has_value()) {
          return std::nullopt;
        }
        items.push_back(*projected_item);
      }
      return json{{"node", "IrPatList"}, {"items", items}};
    }
    case pattern::Kind::Map: {
      json items = json::array();
      for (const auto& [key, value_pattern] : pattern.map_items) {
        auto projected_value = project_pattern(value_pattern);
        if (!projected_value.has_value()) {
          return std::nullopt;
        }
        items.push_back(json{{"key", key}, {"value", *projected_value}});
      }
      return json{{"node", "IrPatMap"}, {"items", items}};
    }
    case pattern::Kind::Err: {
      auto reason = project_pattern(pattern.items[0]);
      auto context = project_pattern(pattern.items[1]);
      if (!reason.has_value() || !context.has_value()) return std::nullopt;
      return json{{"node", "IrPatErr"}, {"reason", *reason}, {"context", *context}};
    }
  }
  return std::nullopt;
}

inline std::optional<json> project_case_clause(const pattern::Pattern& pattern,
                                               const core_ir::Node& result) {
  auto projected_pattern = project_pattern(pattern);
  auto projected_result = project(result);
  if (!projected_pattern.has_value() || !projected_result.has_value()) {
    return std::nullopt;
  }
  return json{
      {"node", "IrCaseClause"}, {"pattern", *projected_pattern}, {"result", *projected_result}};
}

inline std::optional<json> project(const core_ir::Node& node) {
  switch (node.kind) {
    case core_ir::Kind::Literal:
      switch (node.literal_kind) {
        case core_ir::LiteralKind::Integer:
          return json{{"node", "IrLiteral"},
                      {"value", json{{"kind", "integer"}, {"digits", node.integer_digits}}}};
        case core_ir::LiteralKind::Decimal:
          // R21 section 4.2's tagged Decimal payload: both fields are
          // canonical base-10 STRINGS (the exponent included, unlike a
          // JSON number) -- verified directly against
          // spec/ir/r21-decimal-*-tagged-payload.yaml.
          return json{{"node", "IrLiteral"},
                      {"value", json{{"kind", "decimal"},
                                     {"coefficient", node.decimal_coefficient_digits},
                                     {"exponent", std::to_string(node.decimal_exponent)}}}};
        case core_ir::LiteralKind::String:
          return json{{"node", "IrLiteral"}, {"value", node.string_value}};
        case core_ir::LiteralKind::Bool:
          return json{{"node", "IrLiteral"}, {"value", node.bool_value}};
      }
      return std::nullopt;
    case core_ir::Kind::Unary: {
      // hosts/python/ir_normalize.py's real IrUnary handler:
      // {"node": "IrUnary", "op": <token name>, "expr": <ir>} --
      // verified directly against spec/ir/r21-unary-negative-decimal-
      // tagged-payload.yaml.
      auto operand = project(*node.left);
      if (!operand.has_value()) {
        return std::nullopt;
      }
      return json{{"node", "IrUnary"}, {"op", core_ir::op_token_name(node.op)}, {"expr", *operand}};
    }
    case core_ir::Kind::Quote:
    case core_ir::Kind::QuasiQuote: {
      json quoted;
      if (node.left->literal_kind == core_ir::LiteralKind::Integer) {
        try {
          quoted = json{{"kind", "Literal"}, {"value", std::stoll(node.left->integer_digits)}};
        } catch (const std::exception&) {
          return std::nullopt;
        }
      } else if (node.left->literal_kind == core_ir::LiteralKind::Decimal) {
        try {
          quoted = json{{"kind", "Literal"},
                        {"value", std::stod(node.left->decimal_coefficient_digits + "e" +
                                            std::to_string(node.left->decimal_exponent))}};
        } catch (const std::exception&) {
          return std::nullopt;
        }
      } else {
        return std::nullopt;
      }
      return json{{"node", node.kind == core_ir::Kind::Quote ? "IrQuote" : "IrQuasiQuote"},
                  {"expr", quoted}};
    }
    case core_ir::Kind::Var:
      return json{{"node", "IrVar"}, {"name", node.name}};
    case core_ir::Kind::Binary: {
      auto left = project(*node.left);
      auto right = project(*node.right);
      if (!left.has_value() || !right.has_value()) {
        return std::nullopt;
      }
      return json{{"node", "IrBinary"},
                  {"left", *left},
                  {"op", core_ir::op_token_name(node.op)},
                  {"right", *right}};
    }
    case core_ir::Kind::ExprStmt: {
      auto expr = project(*node.left);
      if (!expr.has_value()) {
        return std::nullopt;
      }
      return json{{"node", "IrExprStmt"}, {"expr", *expr}};
    }
    case core_ir::Kind::List: {
      json items = json::array();
      for (const auto& item : node.items) {
        auto projected_item = project(item);
        if (!projected_item.has_value()) {
          return std::nullopt;
        }
        items.push_back(*projected_item);
      }
      return json{{"node", "IrList"}, {"items", items}};
    }
    case core_ir::Kind::Map: {
      json items = json::array();
      for (const auto& [key, value_node] : node.map_entries) {
        auto projected_value = project(value_node);
        if (!projected_value.has_value()) {
          return std::nullopt;
        }
        items.push_back(json{{"key", key}, {"value", *projected_value}});
      }
      return json{{"node", "IrMap"}, {"items", items}};
    }
    case core_ir::Kind::Assign: {
      auto expr = project(*node.left);
      if (!expr.has_value()) {
        return std::nullopt;
      }
      return json{{"node", "IrAssign"}, {"name", node.name}, {"expr", *expr}};
    }
    case core_ir::Kind::Call: {
      json args = json::array();
      for (const auto& arg : node.items) {
        auto projected_arg = project(arg);
        if (!projected_arg.has_value()) {
          return std::nullopt;
        }
        args.push_back(*projected_arg);
      }
      return json{
          {"node", "IrCall"}, {"fn", json{{"node", "IrVar"}, {"name", node.name}}}, {"args", args}};
    }
    case core_ir::Kind::Lambda: {
      auto body = project(*node.left);
      if (!body.has_value()) {
        return std::nullopt;
      }
      json result = {{"node", "IrLambda"}, {"body", *body}};
      if (is_simple_bind_shape(node.params)) {
        json names = json::array();
        for (const auto& p : node.params) {
          names.push_back(p.name);
        }
        result["params"] = names;
      } else {
        result["params"] = json::array();
        auto projected_pattern = project_pattern(pattern::Pattern::tuple(node.params));
        if (!projected_pattern.has_value()) {
          return std::nullopt;
        }
        result["pattern"] = *projected_pattern;
      }
      return result;
    }
    case core_ir::Kind::FuncDef: {
      json body;
      if (node.is_case_body) {
        json clauses = json::array();
        for (size_t i = 0; i < node.case_patterns.size(); ++i) {
          auto clause = project_case_clause(node.case_patterns[i], node.case_results[i]);
          if (!clause.has_value()) {
            return std::nullopt;
          }
          clauses.push_back(*clause);
        }
        body = json{{"node", "IrCase"}, {"clauses", clauses}};
      } else {
        auto projected_body = project(*node.left);
        if (!projected_body.has_value()) {
          return std::nullopt;
        }
        body = *projected_body;
      }
      return json{{"node", "IrFuncDef"},
                  {"name", node.name},
                  {"params", node.header_param_names},
                  {"body", body}};
    }
    case core_ir::Kind::OpenFuncDef: {
      // hosts/python/ir_normalize.py's real IrOpenFuncDef handler:
      // {"node": "IrOpenFuncDef", "name": ..., "clauses": [...]} --
      // `docstring`/`annotations` are only added when non-None/non-empty,
      // and this slice's parser never produces either, so they never
      // appear (verified directly against that source).
      json clauses = json::array();
      for (size_t i = 0; i < node.case_patterns.size(); ++i) {
        auto clause = project_case_clause(node.case_patterns[i], node.case_results[i]);
        if (!clause.has_value()) {
          return std::nullopt;
        }
        clauses.push_back(*clause);
      }
      return json{{"node", "IrOpenFuncDef"}, {"name", node.name}, {"clauses", clauses}};
    }
    case core_ir::Kind::Pipeline: {
      auto source = project(*node.left);
      if (!source.has_value()) {
        return std::nullopt;
      }
      json stages = json::array();
      for (const auto& stage : node.items) {
        auto projected_stage = project(stage);
        if (!projected_stage.has_value()) {
          return std::nullopt;
        }
        stages.push_back(*projected_stage);
      }
      return json{{"node", "IrPipeline"}, {"source", *source}, {"stages", stages}};
    }
    case core_ir::Kind::Spread: {
      auto expr = project(*node.left);
      if (!expr.has_value()) {
        return std::nullopt;
      }
      return json{{"node", "IrSpread"}, {"expr", *expr}};
    }
  }
  return std::nullopt;
}

// hosts/python/ir_normalize.py's normalize_portable_ir always returns a
// JSON array of normalized nodes, one per top-level program statement --
// unlike the `parse`-category projection, it never unwraps a
// single-statement program to a bare node.
inline std::optional<json> project_program(const std::vector<core_ir::Node>& program) {
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

}  // namespace genia::ir_projection
