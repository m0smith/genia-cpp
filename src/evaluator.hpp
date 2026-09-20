// Core IR evaluator for the E24-2/E24-3 vertical slice: exact Integer
// arithmetic, structural equality, list construction, assignment to a
// flat program-level environment, and calls to this slice's native
// functions (map_* / utf8_encode) plus the one bare global name
// (`print`).
//
// Returns std::nullopt whenever the specific IR node cannot be honestly
// evaluated by this slice (an undefined name, a non-integer arithmetic
// operand, a division that would require a Rational this slice does
// not implement, an unrecognized call, ...). Callers must treat
// std::nullopt as "this case is unsupported", never attempt a fallback
// value or a best-effort diagnostic: this project's ambiguity-stop rule
// applies as much to runtime behavior as to parsing.
#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "bignum.hpp"
#include "core_ir.hpp"
#include "equality.hpp"
#include "global_env.hpp"
#include "native_functions.hpp"
#include "value.hpp"

namespace genia::evaluator {

using Environment = std::unordered_map<std::string, value::Value>;

inline std::optional<value::Value> eval_node(const core_ir::Node& node, Environment& env) {
  switch (node.kind) {
    case core_ir::Kind::Literal:
      switch (node.literal_kind) {
        case core_ir::LiteralKind::Integer: {
          auto integer = bignum::Integer::from_unsigned_decimal(node.integer_digits);
          if (!integer.has_value()) {
            return std::nullopt;
          }
          return value::Value::make_integer(*integer);
        }
        case core_ir::LiteralKind::String:
          return value::Value::make_string(node.string_value);
        case core_ir::LiteralKind::Bool:
          return value::Value::make_boolean(node.bool_value);
      }
      return std::nullopt;
    case core_ir::Kind::Var: {
      auto local = env.find(node.name);
      if (local != env.end()) {
        return local->second;
      }
      return global_env::lookup(node.name);
    }
    case core_ir::Kind::ExprStmt:
      return eval_node(*node.left, env);
    case core_ir::Kind::Assign: {
      auto value = eval_node(*node.left, env);
      if (!value.has_value()) {
        return std::nullopt;
      }
      env[node.name] = *value;
      return value;
    }
    case core_ir::Kind::List: {
      std::vector<value::Value> items;
      items.reserve(node.items.size());
      for (const auto& item_node : node.items) {
        auto item_value = eval_node(item_node, env);
        if (!item_value.has_value()) {
          return std::nullopt;
        }
        items.push_back(std::move(*item_value));
      }
      return value::Value::make_list(std::move(items));
    }
    case core_ir::Kind::Call: {
      std::vector<value::Value> args;
      args.reserve(node.items.size());
      for (const auto& arg_node : node.items) {
        auto arg_value = eval_node(arg_node, env);
        if (!arg_value.has_value()) {
          return std::nullopt;
        }
        args.push_back(std::move(*arg_value));
      }
      return native_functions::call(node.name, args);
    }
    case core_ir::Kind::Binary: {
      auto lhs = eval_node(*node.left, env);
      auto rhs = eval_node(*node.right, env);
      if (!lhs.has_value() || !rhs.has_value()) {
        return std::nullopt;
      }
      if (node.op == core_ir::Op::EqEq) {
        return value::Value::make_boolean(equality::structural_equal(*lhs, *rhs));
      }
      if (lhs->kind != value::Kind::Integer || rhs->kind != value::Kind::Integer) {
        return std::nullopt;
      }
      switch (node.op) {
        case core_ir::Op::Plus:
          return value::Value::make_integer(lhs->integer.add(rhs->integer));
        case core_ir::Op::Minus:
          return value::Value::make_integer(lhs->integer.sub(rhs->integer));
        case core_ir::Op::Star:
          return value::Value::make_integer(lhs->integer.mul(rhs->integer));
        case core_ir::Op::Slash: {
          auto quotient = lhs->integer.exact_divide(rhs->integer);
          if (!quotient.has_value()) {
            // Division by zero, or a non-evenly-dividing quotient that
            // R22 defines as producing a Rational: this slice
            // implements neither, so the case is unsupported rather
            // than wrong.
            return std::nullopt;
          }
          return value::Value::make_integer(*quotient);
        }
        case core_ir::Op::EqEq:
          break;  // handled above
      }
      return std::nullopt;
    }
  }
  return std::nullopt;
}

// Evaluates a full program (sequence of independent top-level
// statements), returning the last statement's value -- matching
// genia-2026's block-evaluation model (the value of a program/block is
// the value of its last expression). Assignments introduce bindings
// visible to later statements in the same program, matching genia's
// single-pass top-level block semantics for this slice's flat (non-
// nested-scope) programs. Returns std::nullopt if the program is empty
// or if any statement cannot be evaluated.
inline std::optional<value::Value> eval_program(const std::vector<core_ir::Node>& program) {
  if (program.empty()) {
    return std::nullopt;
  }
  Environment env;
  std::optional<value::Value> result;
  for (const auto& node : program) {
    result = eval_node(node, env);
    if (!result.has_value()) {
      return std::nullopt;
    }
  }
  return result;
}

}  // namespace genia::evaluator
