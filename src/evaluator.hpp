// Core IR evaluator for the E24-2..E24-4 vertical slice: exact Integer
// arithmetic, structural equality, list/map construction, assignment,
// lambdas, named-function definitions (ordinary and local
// case/pattern-dispatch bodies), pipelines, the `err(...)` Outcome
// constructor, and calls to this slice's native functions plus the
// small set of prelude-sourced functions installed by global_env.hpp.
//
// Returns std::nullopt whenever the specific IR node cannot be honestly
// evaluated by this slice (a non-integer arithmetic operand, a division
// that would require a Rational this slice does not implement, an
// unrecognized call, a case/pattern-dispatch with no matching clause,
// ...). Callers must treat std::nullopt as "this case is unsupported",
// never attempt a fallback value or a best-effort diagnostic: this
// project's ambiguity-stop rule applies as much to runtime behavior as
// to parsing.
//
// The one deliberate exception is `UndefinedNameError` (E24-4's
// `deterministic_runtime_error_behavior` evidence): referencing a name
// that is genuinely unbound is not "unsupported" -- genia-2026's real
// reference host normalizes it to a specific, deterministic
// `Error: Undefined name: <name>` diagnostic (exit code 1), verified
// directly against src/genia/environment.py's `NameError` and
// src/genia/interpreter.py's command-mode error handling -- so this is
// a real, evidenced behavior, not a guess. See engine.hpp for where it
// is caught and turned into that exact ok-result.
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "bignum.hpp"
#include "core_ir.hpp"
#include "environment.hpp"
#include "equality.hpp"
#include "genia2026_known_globals.hpp"
#include "global_env.hpp"
#include "lowering.hpp"
#include "native_functions.hpp"
#include "parser.hpp"
#include "pattern_match.hpp"
#include "value.hpp"

namespace genia::evaluator {

struct UndefinedNameError {
  std::string name;
};

// E24-5 (m0smith/genia-2026#959) diagnostic-normalization hardening:
// each Genia-level function/lambda call recurses through several C++
// stack frames (invoke_closure -> eval_node -> ... -> invoke_closure).
// A C++ stack overflow is undefined behavior -- it cannot be caught by
// any try/catch, so an adversarial (or simply long) recursive Genia
// program would otherwise segfault the whole adapter process, an
// unconditional crash and a forbidden-text risk far worse than an
// honest "unsupported" (verified empirically: a valid, non-adversarial
// ~1000-deep recursive `sum_list`-shaped program reliably segfaults this
// adapter without this guard). This limit is deliberately far below
// that measured crash threshold, with comfortable margin, and changes
// no observable behavior for any program shallower than it -- this is
// hardening against a crash, not a new capability or a new Genia
// semantics decision.
constexpr int kMaxCallDepth = 300;
// A per-thread recursion counter is the only reasonable shape for this
// guard in a header-only, free-function evaluator (threading an extra
// parameter through every mutually recursive eval_node/invoke_closure
// call site for one bookkeeping value would be a far more invasive
// change for no behavioral benefit).
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
inline thread_local int g_call_depth = 0;

struct CallDepthGuard {
  CallDepthGuard() { ++g_call_depth; }
  ~CallDepthGuard() { --g_call_depth; }
  CallDepthGuard(const CallDepthGuard&) = delete;
  CallDepthGuard& operator=(const CallDepthGuard&) = delete;
  CallDepthGuard(CallDepthGuard&&) = delete;
  CallDepthGuard& operator=(CallDepthGuard&&) = delete;
};

inline std::optional<value::Value> eval_node(const core_ir::Node& node, const EnvPtr& env);
inline std::optional<value::Value> invoke_closure(const value::Closure& closure,
                                                  const std::vector<value::Value>& args);
inline std::optional<value::Value> eval_pipeline_stage(const core_ir::Node& stage,
                                                       const value::Value& stage_value,
                                                       const EnvPtr& env);

inline std::optional<value::Value> eval_node(const core_ir::Node& node, const EnvPtr& env) {
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
      auto found = env->lookup(node.name);
      if (found.has_value()) {
        return found;
      }
      if (genia2026_known_globals::is_known(node.name)) {
        // A real genia-2026 global this slice has not implemented (e.g.
        // `collect`, `sheet`) -- genuinely bound in the real reference
        // host, so claiming it as "undefined" would be flatly wrong, not
        // merely unimplemented. See genia2026_known_globals.hpp's header
        // comment for why this list exists.
        return std::nullopt;
      }
      throw UndefinedNameError{node.name};
    }
    case core_ir::Kind::ExprStmt:
      return eval_node(*node.left, env);
    case core_ir::Kind::Assign: {
      auto assigned_value = eval_node(*node.left, env);
      if (!assigned_value.has_value()) {
        return std::nullopt;
      }
      env->define(node.name, *assigned_value);
      return assigned_value;
    }
    case core_ir::Kind::List: {
      std::vector<value::Value> items;
      items.reserve(node.items.size());
      for (const auto& item_node : node.items) {
        if (item_node.kind == core_ir::Kind::Spread) {
          auto inner = eval_node(*item_node.left, env);
          if (!inner.has_value() || inner->kind != value::Kind::List) {
            return std::nullopt;
          }
          for (const auto& spread_item : *inner->list_items) {
            items.push_back(spread_item);
          }
          continue;
        }
        auto item_value = eval_node(item_node, env);
        if (!item_value.has_value()) {
          return std::nullopt;
        }
        items.push_back(std::move(*item_value));
      }
      return value::Value::make_list(std::move(items));
    }
    case core_ir::Kind::Map: {
      auto new_map = std::make_shared<value::OrderedMap>();
      for (const auto& [key, value_node] : node.map_entries) {
        auto entry_value = eval_node(value_node, env);
        if (!entry_value.has_value()) {
          return std::nullopt;
        }
        auto key_value = value::Value::make_string(key);
        const std::string encoding = equality::map_key_encoding(key_value);
        new_map->put(encoding, key_value, *entry_value);
      }
      return value::Value::make_map(new_map);
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
      if (node.name == "apply_raw" && args.size() == 2) {
        // `apply_raw(proc, args)` invokes `proc` with `args` directly
        // (genia-2026's src/genia/builtins.py `apply_raw_fn`); it is
        // handled here, not in native_functions.hpp, because invoking a
        // Closure value needs `invoke_closure`, which is this file's
        // evaluator machinery, not an independent value-level
        // computation.
        if (args[0].kind != value::Kind::Closure || args[1].kind != value::Kind::List) {
          return std::nullopt;
        }
        return invoke_closure(*args[0].closure, *args[1].list_items);
      }
      auto callee = env->lookup(node.name);
      if (callee.has_value() && callee->kind == value::Kind::Closure) {
        return invoke_closure(*callee->closure, args);
      }
      return native_functions::call(node.name, args);
    }
    case core_ir::Kind::Lambda: {
      auto closure = std::make_shared<value::Closure>();
      closure->params = node.params;
      closure->body = node.left;
      closure->captured_env = env;
      return value::Value::make_closure(closure);
    }
    case core_ir::Kind::FuncDef: {
      auto closure = std::make_shared<value::Closure>();
      closure->name = node.name;
      closure->captured_env = env;
      if (node.is_case_body) {
        closure->case_clauses.reserve(node.case_patterns.size());
        for (size_t i = 0; i < node.case_patterns.size(); ++i) {
          value::CaseClause clause;
          clause.pattern = node.case_patterns[i];
          clause.result = std::make_shared<core_ir::Node>(node.case_results[i]);
          closure->case_clauses.push_back(std::move(clause));
        }
      } else {
        closure->params = node.params;
        closure->body = node.left;
      }
      auto closure_value = value::Value::make_closure(closure);
      // Bind before returning (not after): a recursive body looks up
      // its own name through `captured_env`, which is this same
      // environment object, so the binding must already be there by
      // the time any call actually runs (matching genia-2026's
      // `env.define_function` -- see src/genia/evaluator.py's IrFuncDef
      // handling).
      env->define(node.name, closure_value);
      return closure_value;
    }
    case core_ir::Kind::Pipeline: {
      auto stage_value = eval_node(*node.left, env);
      if (!stage_value.has_value()) {
        return std::nullopt;
      }
      for (const auto& stage : node.items) {
        if (stage_value->kind == value::Kind::Outcome) {
          // `err(...)` short-circuits the remaining stages unchanged,
          // matching genia-2026's src/genia/evaluator.py
          // `eval_pipeline`'s `is_err(stage_value)` check (this slice
          // has no `none` yet, so only Outcome::Err triggers this).
          return stage_value;
        }
        auto next = eval_pipeline_stage(stage, *stage_value, env);
        if (!next.has_value()) {
          return std::nullopt;
        }
        stage_value = next;
      }
      return stage_value;
    }
    case core_ir::Kind::Spread:
      // Only ever produced inside a list literal, and handled directly
      // by the Kind::List case above -- unreachable as a standalone
      // node (this slice's parser never emits one outside a list).
      return std::nullopt;
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

// Invokes a closure (lambda or named function) with a fully-evaluated
// argument list. A case-dispatch closure (non-empty `case_clauses`)
// tries each clause's pattern against the whole argument list in
// order, matching genia-2026's src/genia/evaluator.py `eval_case_expr`;
// an ordinary closure binds each argument to its positional parameter
// pattern directly. Returns std::nullopt for an arity mismatch or (for
// a case-dispatch closure) when no clause matches -- genia-2026 raises
// a diagnostic-worthy RuntimeError here, which this slice does not yet
// normalize (no pinned evidence needs one), so it is honestly
// unsupported rather than guessed at.
inline std::optional<value::Value> invoke_closure(const value::Closure& closure,
                                                  const std::vector<value::Value>& args) {
  CallDepthGuard depth_guard;
  if (g_call_depth > kMaxCallDepth) {
    return std::nullopt;
  }
  if (!closure.case_clauses.empty()) {
    for (const auto& clause : closure.case_clauses) {
      auto bindings = pattern_match::match(clause.pattern, args);
      if (!bindings.has_value()) {
        continue;
      }
      auto child = std::make_shared<Environment>(closure.captured_env);
      for (auto& binding : *bindings) {
        child->define(binding.first, binding.second);
      }
      return eval_node(*clause.result, child);
    }
    return std::nullopt;
  }
  if (closure.params.size() != args.size()) {
    return std::nullopt;
  }
  auto child = std::make_shared<Environment>(closure.captured_env);
  for (size_t i = 0; i < closure.params.size(); ++i) {
    auto bindings = pattern_match::match_atom(closure.params[i], args[i]);
    if (!bindings.has_value()) {
      return std::nullopt;
    }
    for (auto& binding : *bindings) {
      child->define(binding.first, binding.second);
    }
  }
  return eval_node(*closure.body, child);
}

// Evaluates one pipeline stage against the value piped into it, per
// genia-2026's src/genia/evaluator.py `eval_pipeline_stage`: a call-
// shaped stage (`map(f)`) receives the piped value appended as its
// final positional argument; a bare name stage (`inc`) or any other
// expression stage (e.g. a lambda literal) is evaluated to a callable
// and invoked with the piped value as its sole argument.
inline std::optional<value::Value> eval_pipeline_stage(const core_ir::Node& stage,
                                                       const value::Value& stage_value,
                                                       const EnvPtr& env) {
  if (stage.kind == core_ir::Kind::Call) {
    std::vector<value::Value> args;
    args.reserve(stage.items.size() + 1);
    for (const auto& arg_node : stage.items) {
      auto arg_value = eval_node(arg_node, env);
      if (!arg_value.has_value()) {
        return std::nullopt;
      }
      args.push_back(std::move(*arg_value));
    }
    args.push_back(stage_value);
    auto callee = env->lookup(stage.name);
    if (callee.has_value() && callee->kind == value::Kind::Closure) {
      return invoke_closure(*callee->closure, args);
    }
    return native_functions::call(stage.name, args);
  }
  auto callee = eval_node(stage, env);
  if (!callee.has_value() || callee->kind != value::Kind::Closure) {
    return std::nullopt;
  }
  return invoke_closure(*callee->closure, {stage_value});
}

// Evaluates a full program (sequence of independent top-level
// statements), returning the last statement's value -- matching
// genia-2026's block-evaluation model (the value of a program/block is
// the value of its last expression). Assignments and function
// definitions introduce bindings visible to later statements in the
// same program. Returns std::nullopt if the program is empty or if any
// statement cannot be evaluated.
inline std::optional<value::Value> eval_program(const std::vector<core_ir::Node>& program) {
  if (program.empty()) {
    return std::nullopt;
  }
  auto env = std::make_shared<Environment>();
  env->define("print", value::Value::make_opaque());
  // Install the small prelude-sourced closures (`sum`, `map`,
  // `map_acc`) through the real parse -> lower -> eval pipeline, never
  // a native reimplementation -- see global_env.hpp's header comment.
  // This project's own fixed prelude source is expected to always
  // parse/lower/evaluate successfully; if it somehow did not, those
  // names would simply stay unbound (a later undefined-name error),
  // never a fabricated result.
  auto prelude_program = parser::parse_program(global_env::prelude_source());
  if (prelude_program.has_value()) {
    auto prelude_ir = lowering::lower_program(*prelude_program);
    if (prelude_ir.has_value()) {
      for (const auto& prelude_stmt : *prelude_ir) {
        eval_node(prelude_stmt, env);
      }
    }
  }
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
