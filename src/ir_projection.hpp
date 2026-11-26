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

// Projects supported IR into the portable JSON shape, including present
// docstrings but excluding host-local spans. Unsupported shapes return nullopt.
inline std::optional<json> project(const core_ir::Node& node);

// Projects a required child; a missing (null) child is a malformed carrier.
inline std::optional<json> project_child(const std::shared_ptr<core_ir::Node>& child) {
  if (!child) return std::nullopt;
  return project(*child);
}

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

// Child-count contract of each pattern carrier, checked before any child is
// read: Err/Some hold exactly [value, context], Named exactly [inner],
// Tuple/List any number of items, Map only map_items, and leaf carriers
// (Bind/Wildcard/Rest/Literal) none. Missing or excess children are refused.
inline bool has_valid_arity(const pattern::Pattern& pattern) {
  switch (pattern.kind) {
    case pattern::Kind::Err:
    case pattern::Kind::Some:
      return pattern.items.size() == 2 && pattern.map_items.empty();
    case pattern::Kind::None:
      return pattern.map_items.empty() &&
             (pattern.has_context ? pattern.items.size() == 2 : pattern.items.size() <= 1);
    case pattern::Kind::Named:
      return pattern.items.size() == 1 && pattern.map_items.empty();
    case pattern::Kind::Tuple:
    case pattern::Kind::List:
      return pattern.map_items.empty();
    case pattern::Kind::Map:
      return pattern.items.empty();
    case pattern::Kind::Bind:
    case pattern::Kind::Wildcard:
    case pattern::Kind::Rest:
    case pattern::Kind::Literal:
      return pattern.items.empty() && pattern.map_items.empty();
  }
  return false;
}

// `model_b_header` is true only inside an IrPatternFuncDef header: that is
// the one position where IrPatSome.context is serialized (legacy lambda/case
// positions omit it to keep their historical wire). IrPatErr and IrPatNamed
// serialize identically in every position; an absent IrPatErr context is
// `null`.
inline std::optional<json> project_pattern(const pattern::Pattern& pattern,
                                           bool model_b_header = false) {
  if (!has_valid_arity(pattern)) return std::nullopt;
  switch (pattern.kind) {
    case pattern::Kind::Literal: {
      // genia-2026's real IrPatLiteral carries the raw value (unlike
      // ordinary IrLiteral's R21 tagged numeric payload -- case-pattern
      // numeric literals are explicitly unaffected by that ticket, see
      // pattern.hpp's header comment).
      if (pattern.literal_is_string) return json{{"node", "IrPatLiteral"}, {"value", pattern.name}};
      if (pattern.literal_is_bool) {
        return json{{"node", "IrPatLiteral"}, {"value", pattern.name == "true"}};
      }
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
        auto projected_item = project_pattern(item, model_b_header);
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
        auto projected_item = project_pattern(item, model_b_header);
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
        auto projected_value = project_pattern(value_pattern, model_b_header);
        if (!projected_value.has_value()) {
          return std::nullopt;
        }
        items.push_back(json{{"key", key}, {"value", *projected_value}});
      }
      return json{{"node", "IrPatMap"}, {"items", items}};
    }
    case pattern::Kind::None:
    case pattern::Kind::Err: {
      json reason = nullptr;
      if (!pattern.items.empty()) {
        auto projected_reason = project_pattern(pattern.items[0], model_b_header);
        if (!projected_reason) return std::nullopt;
        reason = *projected_reason;
      }
      json context = nullptr;
      if (pattern.has_context) {
        auto projected_context = project_pattern(pattern.items[1], model_b_header);
        if (!projected_context.has_value()) return std::nullopt;
        context = *projected_context;
      }
      return json{{"node", pattern.kind == pattern::Kind::None ? "IrPatNone" : "IrPatErr"},
                  {"reason", reason},
                  {"context", context}};
    }
    case pattern::Kind::Some: {
      auto inner = project_pattern(pattern.items[0], model_b_header);
      if (!inner.has_value()) return std::nullopt;
      json result{{"node", "IrPatSome"}, {"inner", *inner}};
      if (pattern.has_context) {
        // The context is validated in every position so a malformed subtree
        // fails the whole projection, but it is serialized only inside an
        // IrPatternFuncDef header (legacy positions keep the historical wire).
        auto context = project_pattern(pattern.items[1], model_b_header);
        if (!context.has_value()) return std::nullopt;
        if (model_b_header) result["context"] = *context;
      }
      return result;
    }
    case pattern::Kind::Named: {
      auto inner = project_pattern(pattern.items[0], model_b_header);
      if (!inner.has_value()) return std::nullopt;
      return json{{"node", "IrPatNamed"}, {"name", pattern.name}, {"inner", *inner}};
    }
  }
  return std::nullopt;
}

// Project one existing portable clause, omitting absent guards. Unsupported
// pattern, guard or result projection fails the complete clause without effects.
// Serializes prefix annotations; the caller omits the field when empty.
inline std::optional<json> project_annotations(
    const std::vector<core_ir::Annotation>& annotations) {
  json result = json::array();
  for (const auto& annotation : annotations) {
    auto value = project(annotation.value);
    if (!value) return std::nullopt;
    result.push_back(json{{"node", "IrAnnotation"}, {"name", annotation.name}, {"value", *value}});
  }
  return result;
}

// A grouped (case-body) function needs parallel pattern/result arrays and,
// when guards are present, one guard slot per clause.
inline bool has_valid_clause_arrays(const core_ir::Node& node) {
  return node.case_patterns.size() == node.case_results.size() &&
         (node.case_guards.empty() || node.case_guards.size() == node.case_patterns.size());
}

// True when `node` or any expression beneath it is a function declaration
// (IrFuncDef, IrOpenFuncDef, IrPatternFuncDef or IrClosedFuncDef). Walks every
// child slot: operands, items, map values, bodies, grouped results, guards
// and annotation values. IrImmutableBind is a permitted local binding; its
// initializer is still walked.
inline bool contains_function_declaration(const core_ir::Node& node) {
  switch (node.kind) {
    case core_ir::Kind::FuncDef:
    case core_ir::Kind::OpenFuncDef:
    case core_ir::Kind::PatternFuncDef:
    case core_ir::Kind::ClosedFuncDef:
      return true;
    default:
      break;
  }
  auto any = [](const std::shared_ptr<core_ir::Node>& child) {
    return child && contains_function_declaration(*child);
  };
  if (any(node.left) || any(node.right) || any(node.guard)) return true;
  for (const auto& item : node.items) {
    if (contains_function_declaration(item)) return true;
  }
  for (const auto& result : node.case_results) {
    if (contains_function_declaration(result)) return true;
  }
  for (const auto& guard : node.case_guards) {
    if (any(guard)) return true;
  }
  for (const auto& entry : node.map_entries) {
    if (contains_function_declaration(entry.value)) return true;
  }
  for (const auto& annotation : node.annotations) {
    if (contains_function_declaration(annotation.value)) return true;
  }
  return false;
}

// The body slots of one closed-group child (its declaration itself excluded).
inline bool declaration_body_contains_function(const core_ir::Node& declaration) {
  auto any = [](const std::shared_ptr<core_ir::Node>& child) {
    return child && contains_function_declaration(*child);
  };
  if (any(declaration.left) || any(declaration.guard)) return true;
  for (const auto& result : declaration.case_results) {
    if (contains_function_declaration(result)) return true;
  }
  for (const auto& guard : declaration.case_guards) {
    if (any(guard)) return true;
  }
  return false;
}

// Malformed-carrier validation for direct Model B IR (core-ir-portability.md):
// a pattern declaration needs one full tuple header and no grouped case body;
// a closed group is a nonempty ordered list of same-name IrFuncDef or
// IrPatternFuncDef records, never a nested group, each with a
// body (or parallel clause arrays) that holds no nested function declaration. Invalid carriers are
// not portable IR, so projection refuses them (the case becomes unsupported).
inline bool is_valid_model_b_carrier(const core_ir::Node& node) {
  if (node.kind == core_ir::Kind::PatternFuncDef) {
    return node.header.kind == pattern::Kind::Tuple && node.left != nullptr && !node.is_case_body &&
           node.params.empty() && node.header_param_names.empty() && !node.rest_param.has_value() &&
           !node.docstring.has_value();
  }
  if (node.kind == core_ir::Kind::ClosedFuncDef) {
    if (node.items.empty()) return false;
    for (const auto& declaration : node.items) {
      if (declaration.name != node.name) return false;
      if (declaration.kind != core_ir::Kind::FuncDef &&
          declaration.kind != core_ir::Kind::PatternFuncDef)
        return false;
      if (!is_valid_model_b_carrier(declaration)) return false;
      if (declaration.kind == core_ir::Kind::FuncDef &&
          (declaration.is_case_body ? !has_valid_clause_arrays(declaration)
                                    : declaration.left == nullptr))
        return false;
      if (declaration_body_contains_function(declaration)) return false;
    }
    return true;
  }
  return true;
}

// Serializes one IrCaseClause with its optional guard; nullopt if any child
// is not portable.
inline std::optional<json> project_case_clause(
    const pattern::Pattern& pattern, const core_ir::Node& result,
    const std::shared_ptr<core_ir::Node>& guard = nullptr) {
  auto projected_pattern = project_pattern(pattern);
  auto projected_result = project(result);
  if (!projected_pattern.has_value() || !projected_result.has_value()) {
    return std::nullopt;
  }
  json clause{
      {"node", "IrCaseClause"}, {"pattern", *projected_pattern}, {"result", *projected_result}};
  if (guard) {
    auto expression = project(*guard);
    if (!expression) return std::nullopt;
    clause["guard"] = *expression;
  }
  return clause;
}

// Recursively emits the portable node projection, retaining present metadata
// and omitting host-local spans; an unsupported child fails the whole result.
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
      auto operand = project_child(node.left);
      if (!operand.has_value()) {
        return std::nullopt;
      }
      return json{{"node", "IrUnary"}, {"op", core_ir::op_token_name(node.op)}, {"expr", *operand}};
    }
    case core_ir::Kind::Quote:
    case core_ir::Kind::QuasiQuote: {
      json quoted;
      if (!node.left) return std::nullopt;
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
      auto left = project_child(node.left);
      auto right = project_child(node.right);
      if (!left.has_value() || !right.has_value()) {
        return std::nullopt;
      }
      json projected = json{{"node", "IrBinary"},
                            {"left", *left},
                            {"op", core_ir::op_token_name(node.op)},
                            {"right", *right}};
      // Only ever present (and true) for named-access sugar -- every
      // other Binary case keeps the existing shape with no such key, so
      // already-pinned IrBinary evidence stays byte-for-byte unchanged.
      if (node.named_access) {
        projected["named_access"] = true;
      }
      return projected;
    }
    case core_ir::Kind::ExprStmt: {
      auto expr = project_child(node.left);
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
    case core_ir::Kind::Block: {
      json expressions = json::array();
      for (const auto& expression : node.items) {
        auto projected = project(expression);
        if (!projected.has_value()) return std::nullopt;
        expressions.push_back(*projected);
      }
      return json{{"node", "IrBlock"}, {"exprs", expressions}};
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
      auto expr = project_child(node.left);
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
      auto body = project_child(node.left);
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
      if (node.is_case_body ? !has_valid_clause_arrays(node) : node.left == nullptr)
        return std::nullopt;
      json body;
      if (node.is_case_body) {
        json clauses = json::array();
        for (size_t i = 0; i < node.case_patterns.size(); ++i) {
          auto clause =
              project_case_clause(node.case_patterns[i], node.case_results[i],
                                  node.case_guards.empty() ? nullptr : node.case_guards[i]);
          if (!clause.has_value()) {
            return std::nullopt;
          }
          clauses.push_back(*clause);
        }
        body = json{{"node", "IrCase"}, {"clauses", clauses}};
      } else {
        auto projected_body = project_child(node.left);
        if (!projected_body.has_value()) {
          return std::nullopt;
        }
        body = *projected_body;
      }
      json result{{"node", "IrFuncDef"},
                  {"name", node.name},
                  {"params", node.header_param_names},
                  {"body", body}};
      if (node.docstring.has_value()) result["docstring"] = *node.docstring;
      if (node.rest_param.has_value()) result["rest_param"] = *node.rest_param;
      if (!node.annotations.empty()) {
        auto annotations = project_annotations(node.annotations);
        if (!annotations) return std::nullopt;
        result["annotations"] = *annotations;
      }
      return result;
    }
    case core_ir::Kind::ImmutableBind: {
      auto expr = project_child(node.left);
      if (!expr.has_value()) return std::nullopt;
      json result{{"node", "IrImmutableBind"}, {"name", node.name}, {"expr", *expr}};
      if (!node.annotations.empty()) {
        auto annotations = project_annotations(node.annotations);
        if (!annotations) return std::nullopt;
        result["annotations"] = *annotations;
      }
      return result;
    }
    case core_ir::Kind::PatternFuncDef: {
      if (!is_valid_model_b_carrier(node)) return std::nullopt;
      auto header = project_pattern(node.header, /*model_b_header=*/true);
      auto body = project_child(node.left);
      if (!header || !body) return std::nullopt;
      json result{
          {"node", "IrPatternFuncDef"}, {"name", node.name}, {"pattern", *header}, {"body", *body}};
      if (node.guard) {
        auto guard = project_child(node.guard);
        if (!guard) return std::nullopt;
        result["guard"] = *guard;
      }
      if (!node.annotations.empty()) {
        auto annotations = project_annotations(node.annotations);
        if (!annotations) return std::nullopt;
        result["annotations"] = *annotations;
      }
      return result;
    }
    case core_ir::Kind::ClosedFuncDef: {
      if (!is_valid_model_b_carrier(node)) return std::nullopt;
      json declarations = json::array();
      for (const auto& declaration : node.items) {
        auto projected = project(declaration);
        if (!projected) return std::nullopt;
        declarations.push_back(*projected);
      }
      return json{{"node", "IrClosedFuncDef"}, {"name", node.name}, {"declarations", declarations}};
    }
    case core_ir::Kind::OpenFuncDef: {
      if (!has_valid_clause_arrays(node)) return std::nullopt;
      // hosts/python/ir_normalize.py's real IrOpenFuncDef handler:
      // {"node": "IrOpenFuncDef", "name": ..., "clauses": [...]} --
      // `docstring`/`annotations` are only added when non-None/non-empty,
      // and this slice's parser never produces either, so they never
      // appear (verified directly against that source).
      json clauses = json::array();
      for (size_t i = 0; i < node.case_patterns.size(); ++i) {
        auto clause = project_case_clause(node.case_patterns[i], node.case_results[i],
                                          node.case_guards.empty() ? nullptr : node.case_guards[i]);
        if (!clause.has_value()) {
          return std::nullopt;
        }
        clauses.push_back(*clause);
      }
      return json{{"node", "IrOpenFuncDef"}, {"name", node.name}, {"clauses", clauses}};
    }
    case core_ir::Kind::Pipeline: {
      auto source = project_child(node.left);
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
      auto expr = project_child(node.left);
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
