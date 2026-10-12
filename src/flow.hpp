// R27 E27-1 Flow phase 1 runtime kernel.
//
// A Flow is a lazy, pull-based, single-use sequence (GENIA_STATE.md's Flow
// contract; the reference host's `GeniaFlow`/`GeniaSeq` in
// src/genia/values.py). This header implements only the first-wave
// operators pinned by the `requires: [flow_phase_1]` shared cases in
// spec/flow/: the `stdin |> lines` and list `lines` sources, `evolve`,
// `map`/`filter`/`take`/`drop`/`scan`/`keep_some`/`each` transforms, and the
// `collect`/`run`/`reduce` terminals.
//
// Behavior pinned from the reference host (src/genia/builtins.py and
// src/genia/callable.py), not invented here:
//  - Consuming a Flow marks it consumed; consuming it again raises
//    "Flow has already been consumed".
//  - A downstream Flow consumes its upstream only on the first pull, never
//    when the downstream Flow is merely created or consumed.
//  - `take(n)` reads exactly n items and no more (bounded demand);
//    `evolve` never advances past the demanded item.
//
// Anything outside that evidenced subset (a non-callable stage function, a
// non-Boolean filter predicate, a non-integer count, list-form `scan`,
// none-valued items reaching a callable stage, ...) throws
// `value::UnsupportedError`, which the engine reports as an honest
// `unsupported` result. Nothing here fabricates a diagnostic the reference
// host is not shown to produce.
//
// Finalization: the reference host closes an upstream generator when a
// downstream stage stops early. The C++ Flow kernel owns no resource that
// needs closing (stdin is a shared in-memory line cursor), so early
// termination is simply "stop pulling". There is no close callback here and
// no close-error precedence guarantee; pull/callback errors propagate directly.
// Shared specs expose only stdout,
// stderr and exit code, so no finer-grained finalization is claimed.
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "runtime_io.hpp"
#include "value.hpp"

namespace genia::value {

// A Genia runtime error carrying the reference host's portable message text
// (for example "Flow has already been consumed").
struct FlowError {
  std::string message;
};

// Thrown when a program leaves the evidenced Flow subset; the engine turns
// it into an `unsupported` adapter response.
struct UnsupportedError {};

// Own a factory for one consumption attempt; no concurrent-consumption safety
// or reusable-source contract. Pull copies follow their captures' ownership rules.
class Flow {
 public:
  // A present Value is an item (including Outcome data); nullopt means end.
  using Pull = std::function<std::optional<Value>()>;
  // Produce a pull function when consumed; factories may defer upstream work.
  using Factory = std::function<Pull()>;

  // Move in the factory without running it or consuming an upstream source.
  explicit Flow(Factory factory) : factory_(std::move(factory)) {}

  // Mark consumed before invoking the factory; second attempts throw FlowError,
  // even if factory creation failed. Factory exceptions propagate without reset.
  // Returning a pull does not itself require any upstream item to be read.
  Pull consume() {
    if (consumed_) throw FlowError{"Flow has already been consumed"};
    consumed_ = true;
    return factory_();
  }

 private:
  Factory factory_;
  bool consumed_ = false;
};

}  // namespace genia::value

namespace genia::flow {

using value::Value;
using Pull = value::Flow::Pull;
using Invoke = std::function<std::optional<Value>(const Value&, const std::vector<Value>&)>;

// Runtime type names as they appear in the reference host's diagnostics
// (src/genia/values.py `_runtime_type_name`), for the kinds a Flow or pipe-mode
// diagnostic can be raised over. Other kinds are left unsupported.
inline std::string runtime_type_name(const Value& v) {
  switch (v.kind) {
    case value::Kind::Flow:
      return "flow";
    case value::Kind::StdinSource:
      return "stdin";
    case value::Kind::Integer:
      return "int";
    case value::Kind::String:
      return "string";
    case value::Kind::List:
      return "list";
    case value::Kind::Map:
      return "map";
    case value::Kind::Boolean:
      return "bool";
    default:
      throw value::UnsupportedError{};
  }
}

// The reference host's `_seq_compatible_error`: a Flow/list terminal or stage
// received something that is neither. A bare stdin source gets a hint.
[[noreturn]] inline void seq_compatible_error(const std::string& name, const Value& v) {
  std::string message = name + " expected a Seq-compatible value (list or Flow); received " +
                        runtime_type_name(v) + ".";
  if (v.kind == value::Kind::StdinSource) {
    message += " Use stdin |> lines to adapt stdin into a Flow.";
  }
  throw value::FlowError{message};
}

// Allocate a shared-identity Flow Value owning the moved factory and its captures.
// Allocation errors propagate; construction performs no pull or factory invocation.
inline Value make_flow(value::Flow::Factory factory) {
  return Value::make_flow(std::make_shared<value::Flow>(std::move(factory)));
}

// Defer start until first pull and share the resulting pull state across copies.
// Successful initialization is cached; start/pull exceptions propagate. Failed
// start is not cached, so a later pull retries it without restoring consumed state.
inline Pull lazy(std::function<Pull()> start) {
  auto inner = std::make_shared<std::optional<Pull>>();
  return [start = std::move(start), inner]() -> std::optional<Value> {
    if (!inner->has_value()) *inner = start();
    return (**inner)();
  };
}

// Invoke one Closure with the supplied Values. Non-Closure, none arguments and
// a missing native result throw UnsupportedError in this bounded kernel. Other
// callback exceptions propagate; a present Outcome result remains ordinary data.
inline Value apply(const Invoke& invoke, const Value& fn, const std::vector<Value>& args) {
  if (fn.kind != value::Kind::Closure) throw value::UnsupportedError{};
  for (const auto& arg : args) {
    // The reference host propagates a `none` argument instead of calling the
    // stage; that path is not evidenced, so it is left unsupported.
    if (arg.kind == value::Kind::Outcome && arg.outcome_is_none) throw value::UnsupportedError{};
  }
  auto result = invoke(fn, args);
  if (!result.has_value()) throw value::UnsupportedError{};
  return std::move(*result);
}

// Borrow the Flow owned by a Flow Value; keep the source handle alive while used.
// Non-Flow throws UnsupportedError; valid Flow Values must carry a non-null handle.
inline value::Flow& require_flow(const Value& source) {
  if (source.kind != value::Kind::Flow) throw value::UnsupportedError{};
  return *source.flow;
}

// Require an Integer and convert its decimal text to the native count. More than
// 18 magnitude digits saturate to signed 2^62; this is a bounded host realization,
// not a mathematical unbounded-count guarantee. Non-Integer is UnsupportedError.
inline int64_t count_of(const Value& count) {
  if (count.kind != value::Kind::Integer) throw value::UnsupportedError{};
  const std::string text = count.integer.to_decimal_string();
  const bool negative = !text.empty() && text[0] == '-';
  const size_t digits = negative ? text.size() - 1 : text.size();
  if (digits > 18) return negative ? -(int64_t{1} << 62) : (int64_t{1} << 62);
  return std::stoll(text);
}

// Build a lazy single-use string Flow. stdin captures the shared in-memory cursor;
// list strings are validated eagerly and their shared list retained. Flow input
// is consumed only on first pull and each demanded item is checked then. Unsupported
// source/non-string items throw UnsupportedError; no newline splitting occurs here.
inline Value lines(const Value& source) {
  switch (source.kind) {
    case value::Kind::StdinSource: {
      auto state = runtime_io::g_stdin;
      return make_flow([state]() -> Pull {
        return [state]() -> std::optional<Value> {
          if (state == nullptr || state->position >= state->lines.size()) return std::nullopt;
          return Value::make_string(state->lines[state->position++]);
        };
      });
    }
    case value::Kind::Flow: {
      auto upstream = source.flow;
      return make_flow([upstream]() -> Pull {
        return lazy([upstream]() -> Pull {
          Pull items = upstream->consume();
          return [items]() -> std::optional<Value> {
            auto item = items();
            if (item.has_value() && item->kind != value::Kind::String) {
              throw value::UnsupportedError{};
            }
            return item;
          };
        });
      });
    }
    case value::Kind::List: {
      for (const auto& item : *source.list_items) {
        if (item.kind != value::Kind::String) throw value::UnsupportedError{};
      }
      auto items = source.list_items;
      return make_flow([items]() -> Pull {
        return [items, index = size_t{0}]() mutable -> std::optional<Value> {
          if (index >= items->size()) return std::nullopt;
          return (*items)[index++];
        };
      });
    }
    default:
      throw value::UnsupportedError{};
  }
}

// Produce an unbounded progression: emit init on first pull, then call the Closure
// once per subsequent demanded item. Check the step kind eagerly, defer invocation,
// and retain init/step/invoke captures. Callback errors follow apply. Callers must
// bound unbounded consumption before collect/run.
inline Value evolve(const Value& init, const Value& step, const Invoke& invoke) {
  if (step.kind != value::Kind::Closure) throw value::UnsupportedError{};
  return make_flow([init, step, invoke]() -> Pull {
    return [init, step, invoke, first = true, current = init]() mutable -> std::optional<Value> {
      if (first) {
        first = false;
        return current;
      }
      current = apply(invoke, step, {current});
      return current;
    };
  });
}

// Derive a lazy Flow, retaining fn/upstream/invoke; source must be a valid Flow.
// Each output demands one upstream item and calls apply once. The upstream is
// consumed on first pull; callback validation/errors are deferred to demanded items.
inline Value map(const Value& fn, const Value& source, const Invoke& invoke) {
  auto upstream = source.flow;
  return make_flow([fn, upstream, invoke]() -> Pull {
    return lazy([fn, upstream, invoke]() -> Pull {
      Pull items = upstream->consume();
      return [fn, items, invoke]() -> std::optional<Value> {
        auto item = items();
        if (!item.has_value()) return std::nullopt;
        return apply(invoke, fn, {*item});
      };
    });
  });
}

// Derive a lazy Flow retaining predicate/upstream/invoke; source must be a Flow.
// Pull until a Boolean true verdict or end; false skips an item. Non-Boolean is
// UnsupportedError. One output may require arbitrarily many pulls/callbacks, so an
// unbounded source with no match never returns. Validation follows demanded items.
inline Value filter(const Value& predicate, const Value& source, const Invoke& invoke) {
  auto upstream = source.flow;
  return make_flow([predicate, upstream, invoke]() -> Pull {
    return lazy([predicate, upstream, invoke]() -> Pull {
      Pull items = upstream->consume();
      return [predicate, items, invoke]() -> std::optional<Value> {
        while (true) {
          auto item = items();
          if (!item.has_value()) return std::nullopt;
          const Value verdict = apply(invoke, predicate, {*item});
          if (verdict.kind != value::Kind::Boolean) throw value::UnsupportedError{};
          if (verdict.boolean) return item;
        }
      };
    });
  });
}

// Validate Integer count eagerly and retain the valid Flow upstream. First pull
// consumes upstream, even for a non-positive limit, but reads no item in that case.
// Positive limits demand at most count items with no extra pull after the limit;
// upstream/pull errors propagate and early termination supplies no close hook.
inline Value take(const Value& count, const Value& source) {
  const int64_t limit = count_of(count);
  auto upstream = source.flow;
  return make_flow([limit, upstream]() -> Pull {
    return lazy([limit, upstream]() -> Pull {
      Pull items = upstream->consume();
      return [limit, items, remaining = limit]() mutable -> std::optional<Value> {
        if (remaining <= 0) return std::nullopt;
        auto item = items();
        if (!item.has_value()) {
          remaining = 0;
          return std::nullopt;
        }
        --remaining;
        return item;
      };
    });
  });
}

// Validate Integer count eagerly and lazily consume the valid Flow upstream.
// First output skips up to count items then pulls its item; non-positive skips none.
// Captured upstream is retained, skipped callbacks/effects still happen, and pull
// errors propagate. An unbounded skip is limited by count_of's native saturation.
inline Value drop(const Value& count, const Value& source) {
  const int64_t skip = count_of(count);
  auto upstream = source.flow;
  return make_flow([skip, upstream]() -> Pull {
    return lazy([skip, upstream]() -> Pull {
      Pull items = upstream->consume();
      return [items, remaining = skip]() mutable -> std::optional<Value> {
        while (remaining > 0) {
          --remaining;
          if (!items().has_value()) {
            remaining = 0;
            return std::nullopt;
          }
        }
        return items();
      };
    });
  });
}

// Lazily retain initial/step/upstream/invoke; source must be a valid Flow. For
// each demanded item apply step(state,item), require exactly [new_state, emitted],
// then update local state and emit the second Value. Seed is not emitted; invalid
// result shape is UnsupportedError and callback errors propagate without rollback.
inline Value scan(const Value& step, const Value& initial, const Value& source,
                  const Invoke& invoke) {
  auto upstream = source.flow;
  return make_flow([step, initial, upstream, invoke]() -> Pull {
    return lazy([step, initial, upstream, invoke]() -> Pull {
      Pull items = upstream->consume();
      return [step, items, invoke, state = initial]() mutable -> std::optional<Value> {
        auto item = items();
        if (!item.has_value()) return std::nullopt;
        const Value result = apply(invoke, step, {state, *item});
        if (result.kind != value::Kind::List || result.list_items->size() != 2) {
          throw value::UnsupportedError{};
        }
        state = (*result.list_items)[0];
        return (*result.list_items)[1];
      };
    });
  });
}

// Lazily consume a valid Flow, skipping none and unwrapping some in source order.
// Raw values or err Outcomes throw UnsupportedError in this bounded helper. A pull
// may scan arbitrarily many skipped items; upstream captures remain owned by it.
inline Value keep_some(const Value& source) {
  auto upstream = source.flow;
  return make_flow([upstream]() -> Pull {
    return lazy([upstream]() -> Pull {
      Pull items = upstream->consume();
      return [items]() -> std::optional<Value> {
        while (true) {
          auto item = items();
          if (!item.has_value()) return std::nullopt;
          if (item->kind != value::Kind::Outcome || item->outcome_is_err) {
            throw value::UnsupportedError{};
          }
          if (item->outcome_is_none) continue;
          return *item->outcome_value;
        }
      };
    });
  });
}

// Retain a list or valid Flow and lazily invoke effect once per demanded item,
// returning that same item; callback result is ignored but apply errors propagate.
// List input is not eager iteration; Flow input consumes upstream on first pull.
// Captures own source/effect/invoke and already-performed effects are not rolled back.
inline Value each(const Value& effect, const Value& source, const Invoke& invoke) {
  if (source.kind == value::Kind::List) {
    auto items = source.list_items;
    return make_flow([effect, items, invoke]() -> Pull {
      return [effect, items, invoke, index = size_t{0}]() mutable -> std::optional<Value> {
        if (index >= items->size()) return std::nullopt;
        const Value item = (*items)[index++];
        apply(invoke, effect, {item});
        return item;
      };
    });
  }
  auto upstream = source.flow;
  return make_flow([effect, upstream, invoke]() -> Pull {
    return lazy([effect, upstream, invoke]() -> Pull {
      Pull items = upstream->consume();
      return [effect, items, invoke]() -> std::optional<Value> {
        auto item = items();
        if (!item.has_value()) return std::nullopt;
        apply(invoke, effect, {*item});
        return item;
      };
    });
  });
}

// Eagerly materialize a list/Flow in order, copying a list or consuming a Flow
// once. An unbounded Flow may never finish and storage is not bounded. Pull and
// allocation failures propagate; nested Value handles retain shared identity.
// Non-compatible sources use seq_compatible_error; no close hook is called.
inline Value collect(const Value& source) {
  if (source.kind == value::Kind::List) return Value::make_list(*source.list_items);
  if (source.kind != value::Kind::Flow) seq_compatible_error("collect", source);
  Pull items = require_flow(source).consume();
  std::vector<Value> collected;
  while (auto item = items()) collected.push_back(std::move(*item));
  return Value::make_list(std::move(collected));
}

// Drain a Flow once for effects and return nil; a list returns nil without
// traversing its items here. An unbounded Flow may never finish; pull errors
// propagate and incompatible input uses seq_compatible_error. No close hook exists.
inline Value run(const Value& source) {
  if (source.kind != value::Kind::List) {
    if (source.kind != value::Kind::Flow) seq_compatible_error("run", source);
    Pull items = require_flow(source).consume();
    while (items().has_value()) {
    }
  }
  return Value::make_nil();
}

// `_seq_reduce(f, acc, source)`: the non-list branch of the prelude
// `reduce`. The reducer is invoked without none-propagation, like the
// reference host's `_invoke_raw_from_builtin`.
inline Value reduce(const Value& reducer, Value accumulator, const Value& source,
                    const Invoke& invoke) {
  auto step = [&](const Value& item) {
    if (reducer.kind != value::Kind::Closure) throw value::UnsupportedError{};
    auto result = invoke(reducer, {accumulator, item});
    if (!result.has_value()) throw value::UnsupportedError{};
    accumulator = std::move(*result);
  };
  if (source.kind == value::Kind::List) {
    for (const auto& item : *source.list_items) step(item);
    return accumulator;
  }
  if (source.kind != value::Kind::Flow) seq_compatible_error("reduce", source);
  Pull items = require_flow(source).consume();
  while (auto item = items()) step(*item);
  return accumulator;
}

// Routes a call by name to the Flow kernel. Returns true and sets `out` when
// `name`/arity/argument-kinds select a Flow operation; false leaves the call
// to ordinary dispatch. The Flow forms of `map`/`filter`/`take`/`drop` are
// selected by the *last argument* being a Flow, matching the reference host's
// call-site dispatch (src/genia/callable.py `invoke_callable`).
inline bool dispatch(const std::string& name, const std::vector<Value>& args, const Invoke& invoke,
                     Value& out) {
  const size_t n = args.size();
  const bool last_is_flow = n > 0 && args.back().kind == value::Kind::Flow;
  if (name == "lines" && n == 1) {
    out = lines(args[0]);
    return true;
  }
  if (name == "evolve" && n == 2) {
    out = evolve(args[0], args[1], invoke);
    return true;
  }
  if (name == "map" && n == 2 && last_is_flow) {
    out = map(args[0], args[1], invoke);
    return true;
  }
  if (name == "filter" && n == 2 && last_is_flow) {
    out = filter(args[0], args[1], invoke);
    return true;
  }
  if (name == "take" && n == 2 && last_is_flow) {
    out = take(args[0], args[1]);
    return true;
  }
  if (name == "drop" && n == 2 && last_is_flow) {
    out = drop(args[0], args[1]);
    return true;
  }
  if (name == "scan" && n == 3) {
    if (args[2].kind != value::Kind::List && !last_is_flow) seq_compatible_error("scan", args[2]);
    if (!last_is_flow) throw value::UnsupportedError{};
    out = scan(args[0], args[1], args[2], invoke);
    return true;
  }
  if (name == "keep_some" && n == 1) {
    if (!last_is_flow) {
      throw value::FlowError{"keep_some expected a flow, received " + runtime_type_name(args[0])};
    }
    out = keep_some(args[0]);
    return true;
  }
  if (name == "each" && n == 2) {
    if (args[1].kind != value::Kind::List && !last_is_flow) seq_compatible_error("each", args[1]);
    out = each(args[0], args[1], invoke);
    return true;
  }
  if (name == "collect" && n == 1) {
    out = collect(args[0]);
    return true;
  }
  if (name == "run" && n == 1) {
    out = run(args[0]);
    return true;
  }
  if (name == "_pipe_run" && n == 1) {
    // Pipe mode's implicit final stage: only a Flow may be run.
    if (!last_is_flow) {
      throw value::FlowError{"run expected a flow, received " + runtime_type_name(args[0])};
    }
    out = run(args[0]);
    return true;
  }
  if (name == "_seq_type_error" && n == 2 && args[0].kind == value::Kind::String) {
    // Reached from the prelude's list helpers when their argument is neither a
    // list nor a Flow.
    seq_compatible_error(args[0].text, args[1]);
  }
  if (name == "_seq_reduce" && n == 3) {
    out = reduce(args[0], args[1], args[2], invoke);
    return true;
  }
  return false;
}

}  // namespace genia::flow
