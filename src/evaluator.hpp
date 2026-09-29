// Core IR evaluator for the E24-2..E24-7 vertical slice: exact
// Integer/Decimal/Rational arithmetic (see arithmetic.hpp for the R22
// section 6-8 promotion/division/remainder rules), structural equality,
// list/map construction, assignment, lambdas, named-function
// definitions (ordinary and local case/pattern-dispatch bodies),
// pipelines, the `err(...)` Outcome constructor, and calls to this
// slice's native functions plus the small set of prelude-sourced
// functions installed by global_env.hpp.
//
// Returns std::nullopt whenever the specific IR node cannot be honestly
// evaluated by this slice (a non-numeric arithmetic operand, mixed
// Float64/exact arithmetic this slice does not implement, an
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

#include "arithmetic.hpp"
#include "bignum.hpp"
#include "core_ir.hpp"
#include "environment.hpp"
#include "equality.hpp"
#include "flow.hpp"
#include "genia2026_known_globals.hpp"
#include "global_env.hpp"
#include "lowering.hpp"
#include "native_functions.hpp"
#include "parser.hpp"
#include "pattern_match.hpp"
#include "render.hpp"
#include "runtime_io.hpp"
#include "value.hpp"

namespace genia::evaluator {

struct UndefinedNameError {
  std::string name;
};

struct StatefulRuntimeError {
  std::string message;
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

// R27 E27-1: routes a named call to the Flow kernel (flow.hpp) when the
// call selects a Flow operation. Returns false to leave the call to ordinary
// closure/native dispatch.
inline bool dispatch_flow(const std::string& name, const std::vector<value::Value>& args,
                          value::Value& out) {
  const flow::Invoke invoke = [](const value::Value& fn, const std::vector<value::Value>& a) {
    return invoke_closure(*fn.closure, a);
  };
  return flow::dispatch(name, args, invoke, out);
}

// Calls a first-class native reference (for example `upper` passed to
// `map`) by name.
inline std::optional<value::Value> call_native_reference(const std::string& name,
                                                         const std::vector<value::Value>& args) {
  value::Value flow_result;
  if (dispatch_flow(name, args, flow_result)) return flow_result;
  return native_functions::call(name, args);
}

// A Genia closure bound to `name` wins over the Flow kernel's own names,
// except for `map`/`filter`/`take`/`drop`, whose Flow forms the reference
// host selects by the last argument being a Flow regardless of the binding
// (src/genia/callable.py `invoke_callable`).
inline bool flow_dispatch_applies(const std::string& name, bool bound_genia_closure) {
  return !bound_genia_closure || name == "map" || name == "filter" || name == "take" ||
         name == "drop";
}

// Mirrors `_render_pipeline_stage` for the stage shapes the Flow cases can
// reach: a bare name, or a call whose arguments are names, integer/string
// literals, or lambdas. Anything else is left unsupported.
inline std::string render_stage_text(const core_ir::Node& node) {
  switch (node.kind) {
    case core_ir::Kind::Var:
      return node.name;
    case core_ir::Kind::Literal:
      if (node.literal_kind == core_ir::LiteralKind::Integer) return node.integer_digits;
      if (node.literal_kind == core_ir::LiteralKind::String) {
        auto quoted = render::display(value::Value::make_string(node.string_value));
        if (quoted.has_value()) return *quoted;
      }
      throw value::UnsupportedError{};
    case core_ir::Kind::Lambda: {
      std::string params;
      for (const auto& param : node.params) {
        if (param.kind != pattern::Kind::Bind) throw value::UnsupportedError{};
        params += params.empty() ? param.name : ", " + param.name;
      }
      return "(" + params + ") -> ...";
    }
    case core_ir::Kind::Call: {
      std::string rendered = node.name + "(";
      for (size_t i = 0; i < node.items.size(); ++i) {
        rendered += (i == 0 ? "" : ", ") + render_stage_text(node.items[i]);
      }
      return rendered + ")";
    }
    default:
      throw value::UnsupportedError{};
  }
}

// Mirrors `_wrap_pipeline_stage_error`: a runtime error raised while a
// pipeline stage runs is re-raised with the stage number, mode, rendered
// stage and source span, unless it already carries a stage prefix.
inline value::FlowError wrap_pipeline_stage_error(const value::FlowError& error, size_t index,
                                                  const core_ir::Node& stage,
                                                  const value::Value& stage_input) {
  const std::string& message = error.message;
  if (message.rfind("pipeline stage ", 0) == 0 &&
      message.find(" failed in ") != std::string::npos) {
    return error;
  }
  const std::string stage_name =
      (stage.kind == core_ir::Kind::Var || stage.kind == core_ir::Kind::Call) ? stage.name : "";
  const bool input_is_flow = stage_input.kind == value::Kind::Flow;
  std::string mode = "Value mode";
  if (stage_name == "lines" || (stage_name == "collect" && input_is_flow)) {
    mode = "Explicit bridge mode";
  } else if (input_is_flow) {
    mode = "Flow mode";
  }
  // The span's file name is only known for command-source execution.
  if (runtime_io::g_source_name.empty() || stage.line <= 0) throw value::UnsupportedError{};
  std::string rendered = "pipeline stage " + std::to_string(index + 1) + " failed in " + mode +
                         " at " + render_stage_text(stage) + " [" + runtime_io::g_source_name +
                         ":" + std::to_string(stage.line) + "]";
  rendered += ": stage received " + flow::runtime_type_name(stage_input);
  if (!message.empty()) rendered += "; " + message;
  return value::FlowError{rendered};
}

// Mirrors src/genia/builtins.py's `_runtime_type_name` closely enough
// for the exact set of kinds `json_decode`'s misuse diagnostic can
// actually receive in this slice's pinned evidence (only `int` today);
// not a general-purpose type-name registry.
inline std::string json_runtime_type_name(value::Kind kind) {
  switch (kind) {
    case value::Kind::Integer:
      return "int";
    case value::Kind::Decimal:
      return "decimal";
    case value::Kind::Rational:
      return "rational";
    case value::Kind::Float64:
      return "float";
    case value::Kind::Boolean:
      return "bool";
    case value::Kind::List:
      return "list";
    case value::Kind::Map:
      return "map";
    default:
      return "value";
  }
}

inline const char* arithmetic_symbol(core_ir::Op op) {
  switch (op) {
    case core_ir::Op::Plus:
      return "+";
    case core_ir::Op::Minus:
      return "-";
    case core_ir::Op::Star:
      return "*";
    case core_ir::Op::Slash:
      return "/";
    case core_ir::Op::Percent:
      return "%";
    default:
      return "";
  }
}

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
        case core_ir::LiteralKind::Decimal: {
          auto coefficient =
              bignum::Integer::from_unsigned_decimal(node.decimal_coefficient_digits);
          if (!coefficient.has_value()) {
            return std::nullopt;
          }
          return value::Value::make_decimal(*coefficient, node.decimal_exponent);
        }
        case core_ir::LiteralKind::String:
          return value::Value::make_string(node.string_value);
        case core_ir::LiteralKind::Bool:
          return value::Value::make_boolean(node.bool_value);
      }
      return std::nullopt;
    case core_ir::Kind::Unary: {
      // R22 section 4: Decimal negation preserves canonical form
      // trivially (negating a canonical coefficient never introduces a
      // trailing zero or changes zero-ness). Only MINUS is in this
      // slice's grammar (parser.hpp never produces another unary op).
      if (node.op != core_ir::Op::Minus) {
        return std::nullopt;
      }
      auto operand = eval_node(*node.left, env);
      if (!operand.has_value()) {
        return std::nullopt;
      }
      if (operand->kind == value::Kind::Integer) {
        return value::Value::make_integer(operand->integer.negate());
      }
      if (operand->kind == value::Kind::Decimal) {
        return value::Value::make_decimal(operand->decimal_coefficient.negate(),
                                          operand->decimal_exponent);
      }
      if (operand->kind == value::Kind::Rational) {
        // Denominator is already positive and unaffected by negation;
        // only the sign-carrying numerator flips (R22 section 3).
        return value::Value::make_rational(operand->rational_numerator.negate(),
                                           operand->rational_denominator);
      }
      if (operand->kind == value::Kind::Float64) {
        return float64::negate(*operand);
      }
      return std::nullopt;
    }
    case core_ir::Kind::Var: {
      auto found = env->lookup(node.name);
      if (found.has_value()) {
        return found;
      }
      if (native_functions::is_first_class(node.name)) {
        return value::Value::make_native_ref(node.name);
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
    case core_ir::Kind::Quote:
    case core_ir::Kind::QuasiQuote:
      // R22's required evidence uses only self-evaluating numeric literals.
      // The parser rejects every other quoted form, so evaluating the child
      // here is observably identical without claiming general quote support.
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
    case core_ir::Kind::Block: {
      std::optional<value::Value> result;
      for (const auto& expression : node.items) {
        result = eval_node(expression, env);
        if (!result.has_value()) return std::nullopt;
      }
      return result;
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
      if (node.name == "empty_env" && args.empty()) {
        return value::Value::make_opaque();
      }
      if (node.name == "eval" && args.size() == 2 && args[1].kind == value::Kind::Opaque &&
          equality::is_exact_family_kind(args[0].kind)) {
        return args[0];
      }
      if (node.name == "ref_update" && args.size() == 2 && args[0].kind == value::Kind::Ref &&
          args[1].kind == value::Kind::Closure) {
        std::optional<value::Value> replacement;
        auto result = args[0].ref->update([&](const value::Value& current) {
          replacement = invoke_closure(*args[1].closure, {current});
          return replacement.value_or(current);
        });
        if (!replacement.has_value()) return std::nullopt;
        return result;
      }
      if (node.name == "cell_send" && args.size() == 2 && args[0].kind == value::Kind::Cell &&
          args[1].kind == value::Kind::Closure) {
        auto target = args[0].cell;
        auto closure = args[1].closure;
        auto accept = [target, closure] {
          target->send([closure](const value::Value& current) {
            return invoke_closure(*closure, {current});
          });
        };
        if (!value::stage_cell_send(accept)) accept();
        return value::Value::make_outcome_none(value::Value::make_string("nil"));
      }
      if (node.name == "spawn" && args.size() == 1 && args[0].kind == value::Kind::Closure) {
        auto closure = args[0].closure;
        return value::Value::make_process(
            value::Process::create([closure](const value::Value& message) {
              return invoke_closure(*closure, {message});
            }));
      }
      if (node.name == "send" && args.size() == 2 && args[0].kind == value::Kind::Process) {
        auto process = args[0].process;
        auto message = args[1];
        auto accept = [process, message] {
          if (!process->send(message)) throw StatefulRuntimeError{"send: process is failed"};
        };
        if (!value::stage_cell_send(accept)) accept();
        return value::Value::make_outcome_none(value::Value::make_string("nil"));
      }
      if (node.name == "representation_match" && args.size() == 2 &&
          args[0].kind == value::Kind::String && args[0].text == "secret") {
        throw StatefulRuntimeError{
            "representation_match cannot use reserved protected facet \"secret\""};
      }
      if (node.name == "represent" && args.size() == 2 && args[0].kind == value::Kind::String) {
        // genia-2026's real `represent(facet, value)` raises these two
        // programmer-misuse `TypeError`/`ValueError` diagnostics before
        // constructing anything (src/genia/builtins.py's `represent_fn`).
        if (args[0].text.empty()) {
          throw StatefulRuntimeError{"represent expected a non-empty facet string"};
        }
        if (args[0].text == "secret") {
          throw StatefulRuntimeError{"represent cannot use reserved protected facet \"secret\""};
        }
      }
      if (node.name == "json_decode" && args.size() == 1 && args[0].kind != value::Kind::String &&
          args[0].kind != value::Kind::Bytes) {
        // R26-2 contract, `error-json-decode-input-type.yaml`:
        // genia-2026's real `json_decode_fn` raises this exact
        // programmer-misuse `TypeError` (never a recoverable Outcome)
        // for a non-string/Bytes argument. `_runtime_type_name` mirrors
        // src/genia/builtins.py's own helper closely enough for the one
        // pinned kind (`int`) this evidence exercises.
        throw StatefulRuntimeError{"json_decode expected string or bytes, received " +
                                   json_runtime_type_name(args[0].kind)};
      }
      auto callee = env->lookup(node.name);
      const bool bound_closure = callee.has_value() && callee->kind == value::Kind::Closure &&
                                 callee->closure->native_name.empty();
      value::Value flow_result;
      if (flow_dispatch_applies(node.name, bound_closure) &&
          dispatch_flow(node.name, args, flow_result)) {
        return flow_result;
      }
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
    // E24-6: a local open function's runtime representation is
    // identical to a case-body FuncDef's -- dispatching against only
    // its own clauses IS the whole contract §5 algorithm when exactly
    // one unit participates (docs/design/r20-open-functions-syntax-ir-
    // design.md section 5's `GeniaOpenFunction`, "dispatches using only
    // its own clauses as the single participating unit"). No new
    // dispatch code, so both Kinds share this block.
    case core_ir::Kind::OpenFuncDef:
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
      size_t stage_index = 0;
      for (const auto& stage : node.items) {
        if (stage_value->kind == value::Kind::Outcome && stage_value->outcome_is_err) {
          // `err(...)` short-circuits the remaining stages unchanged,
          // matching genia-2026's src/genia/evaluator.py
          // `eval_pipeline`'s `is_err(stage_value)` check (this slice
          // has no `none` yet, so only Outcome::Err triggers this).
          return stage_value;
        }
        std::optional<value::Value> next;
        try {
          next = eval_pipeline_stage(stage, *stage_value, env);
        } catch (const value::FlowError& error) {
          throw wrap_pipeline_stage_error(error, stage_index, stage, *stage_value);
        } catch (const UndefinedNameError& error) {
          // The reference host wraps every runtime error raised inside a
          // stage (src/genia/evaluator.py `_wrap_pipeline_stage_error`), not
          // only Flow errors; the message text is preserved.
          throw wrap_pipeline_stage_error(value::FlowError{"Undefined name: " + error.name},
                                          stage_index, stage, *stage_value);
        } catch (const StatefulRuntimeError& error) {
          throw wrap_pipeline_stage_error(value::FlowError{error.message}, stage_index, stage,
                                          *stage_value);
        } catch (const format::FormatError& error) {
          throw wrap_pipeline_stage_error(value::FlowError{error.message}, stage_index, stage,
                                          *stage_value);
        } catch (const float64::MagnitudeOverflowError&) {
          throw wrap_pipeline_stage_error(
              value::FlowError{
                  "float64: exact magnitude exceeds the largest finite binary64 value"},
              stage_index, stage, *stage_value);
        } catch (const float64::DivisionByZeroError&) {
          throw wrap_pipeline_stage_error(value::FlowError{"float64 division by zero"}, stage_index,
                                          stage, *stage_value);
        } catch (const float64::RemainderByZeroError&) {
          throw wrap_pipeline_stage_error(value::FlowError{"float64 remainder by zero"},
                                          stage_index, stage, *stage_value);
        }
        if (!next.has_value()) {
          return std::nullopt;
        }
        stage_value = next;
        ++stage_index;
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
      if (!lhs.has_value()) {
        return std::nullopt;
      }
      if (node.op == core_ir::Op::Slash && node.named_access) {
        // genia-2026's `x.y` named-access sugar (evaluator.py's
        // `eval_binary` SLASH/named_access case): the right side is
        // never evaluated as a binding reference -- its raw `name` is
        // used as a direct (non-Option) Map key lookup. Module export
        // access has no equivalent in this slice (no module system).
        if (lhs->kind != value::Kind::Map || node.right->kind != core_ir::Kind::Var) {
          return std::nullopt;
        }
        auto key_value = value::Value::make_string(node.right->name);
        auto key_encoding = equality::map_key_encoding_checked(key_value);
        if (!key_encoding.has_value()) return std::nullopt;
        const value::Value* found = lhs->map->get(*key_encoding);
        if (found == nullptr) return std::nullopt;
        return *found;
      }
      auto rhs = eval_node(*node.right, env);
      if (!rhs.has_value()) {
        return std::nullopt;
      }
      if (node.op == core_ir::Op::EqEq) {
        return value::Value::make_boolean(equality::structural_equal(*lhs, *rhs));
      }
      if (node.op == core_ir::Op::NotEq) {
        // R18: equality is ONE relation (==, !=); != is exactly the
        // logical negation of == (verified directly against the
        // reference host, not guessed).
        return value::Value::make_boolean(!equality::structural_equal(*lhs, *rhs));
      }
      if (node.op == core_ir::Op::Plus && lhs->kind == value::Kind::String &&
          rhs->kind == value::Kind::String) {
        // genia-2026's `+` on two strings is concatenation (the same
        // Python `+` operator `eval_binary` falls through to below,
        // before any exact-family numeric gating applies).
        return value::Value::make_string(lhs->text + rhs->text);
      }
      if ((node.op == core_ir::Op::Lt || node.op == core_ir::Op::Le || node.op == core_ir::Op::Gt ||
           node.op == core_ir::Op::Ge) &&
          equality::is_numeric_kind(lhs->kind) && equality::is_numeric_kind(rhs->kind)) {
        const auto order = equality::numeric_compare(*lhs, *rhs);
        switch (node.op) {
          case core_ir::Op::Lt:
            return value::Value::make_boolean(order == equality::NumericOrder::Less);
          case core_ir::Op::Le:
            return value::Value::make_boolean(order == equality::NumericOrder::Less ||
                                              order == equality::NumericOrder::Equal);
          case core_ir::Op::Gt:
            return value::Value::make_boolean(order == equality::NumericOrder::Greater);
          case core_ir::Op::Ge:
            return value::Value::make_boolean(order == equality::NumericOrder::Greater ||
                                              order == equality::NumericOrder::Equal);
          default:
            break;
        }
      }
      if (lhs->kind == value::Kind::Float64 || rhs->kind == value::Kind::Float64) {
        // R22 section 9: Float64 arithmetic is a closed domain. Both
        // operands must already be Float64; exact-family mixing remains
        // rejected rather than entering the exact promotion lattice.
        const bool mixed =
            (lhs->kind == value::Kind::Float64 && equality::is_exact_family_kind(rhs->kind)) ||
            (rhs->kind == value::Kind::Float64 && equality::is_exact_family_kind(lhs->kind));
        if (mixed) {
          auto kind_name = [](value::Kind kind) -> std::string {
            if (kind == value::Kind::Integer) return "int";
            if (kind == value::Kind::Decimal) return "decimal";
            if (kind == value::Kind::Rational) return "rational";
            return "float";
          };
          auto context = std::make_shared<value::OrderedMap>();
          for (const auto& [key, mapped] : std::vector<std::pair<std::string, std::string>>{
                   {"source", arithmetic_symbol(node.op)},
                   {"left", kind_name(lhs->kind)},
                   {"right", kind_name(rhs->kind)}}) {
            auto key_value = value::Value::make_string(key);
            context->put(equality::map_key_encoding(key_value), key_value,
                         value::Value::make_string(mapped));
          }
          return value::Value::make_outcome_none(value::Value::make_string("type-error"),
                                                 value::Value::make_map(context));
        }
        return float64::arithmetic(node.op, *lhs, *rhs);
      }
      if (!equality::is_exact_family_kind(lhs->kind) ||
          !equality::is_exact_family_kind(rhs->kind)) {
        return std::nullopt;
      }
      // R22 section 6 promotion lattice (Integer < Decimal < Rational)
      // for `+`/`-`/`*` and (section 8) `%`; section 7's own table for
      // `/`. `both_integer` keeps the fast bignum-only path for the
      // overwhelmingly common case; `any_rational` routes to general
      // exact fraction algebra (reduced, collapsing to Integer at
      // denominator 1); otherwise exactly one operand is Decimal (the
      // other Integer or Decimal), which retains Decimal per section 6.
      const bool both_integer =
          lhs->kind == value::Kind::Integer && rhs->kind == value::Kind::Integer;
      const bool any_rational =
          lhs->kind == value::Kind::Rational || rhs->kind == value::Kind::Rational;
      switch (node.op) {
        case core_ir::Op::Plus:
          if (both_integer) {
            return value::Value::make_integer(lhs->integer.add(rhs->integer));
          }
          if (any_rational) {
            return arithmetic::rational_add(*lhs, *rhs);
          }
          return arithmetic::decimal_add_sub(*lhs, *rhs, /*subtract=*/false);
        case core_ir::Op::Minus:
          if (both_integer) {
            return value::Value::make_integer(lhs->integer.sub(rhs->integer));
          }
          if (any_rational) {
            return arithmetic::rational_sub(*lhs, *rhs);
          }
          return arithmetic::decimal_add_sub(*lhs, *rhs, /*subtract=*/true);
        case core_ir::Op::Star:
          if (both_integer) {
            return value::Value::make_integer(lhs->integer.mul(rhs->integer));
          }
          if (any_rational) {
            return arithmetic::rational_mul(*lhs, *rhs);
          }
          return arithmetic::decimal_mul(*lhs, *rhs);
        case core_ir::Op::Slash:
          // arithmetic::exact_divide implements R22 section 7's full
          // table (Integer-or-Rational for Integer/Integer, Decimal
          // when the quotient terminates in base 10, Rational
          // otherwise); std::nullopt only for division by exact zero.
          return arithmetic::exact_divide(*lhs, *rhs);
        case core_ir::Op::Percent:
          return arithmetic::exact_floor_remainder(*lhs, *rhs);
        // R22 section 10.1: Integer/Decimal/Rational compare by
        // mathematical value for <, <=, >, >=, via the same
        // numerator/denominator cross-multiplication `==`/`!=` already
        // use (equality.hpp's `exact_family_compare`) -- never rounding
        // through a host binary float.
        case core_ir::Op::Lt:
          return value::Value::make_boolean(equality::exact_family_compare(*lhs, *rhs) < 0);
        case core_ir::Op::Le:
          return value::Value::make_boolean(equality::exact_family_compare(*lhs, *rhs) <= 0);
        case core_ir::Op::Gt:
          return value::Value::make_boolean(equality::exact_family_compare(*lhs, *rhs) > 0);
        case core_ir::Op::Ge:
          return value::Value::make_boolean(equality::exact_family_compare(*lhs, *rhs) >= 0);
        case core_ir::Op::EqEq:
        case core_ir::Op::NotEq:
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
  if (!closure.native_name.empty()) {
    return call_native_reference(closure.native_name, args);
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
    const bool bound_closure = callee.has_value() && callee->kind == value::Kind::Closure &&
                               callee->closure->native_name.empty();
    value::Value flow_result;
    if (flow_dispatch_applies(stage.name, bound_closure) &&
        dispatch_flow(stage.name, args, flow_result)) {
      return flow_result;
    }
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
inline EnvPtr new_session_environment() {
  auto env = std::make_shared<Environment>();
  env->define("print", value::Value::make_native_ref("print"));
  env->define("stdin", value::Value::make_stdin_source());
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
  return env;
}

inline std::optional<value::Value> eval_in_environment(const std::vector<core_ir::Node>& program,
                                                       const EnvPtr& env) {
  if (program.empty()) return std::nullopt;
  std::optional<value::Value> result;
  for (const auto& node : program) {
    result = eval_node(node, env);
    if (!result.has_value()) {
      return std::nullopt;
    }
  }
  return result;
}

inline std::optional<value::Value> eval_program(const std::vector<core_ir::Node>& program) {
  return eval_in_environment(program, new_session_environment());
}

}  // namespace genia::evaluator
