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
// termination is simply "stop pulling"; shared specs expose only stdout,
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

class Flow {
 public:
  using Pull = std::function<std::optional<Value>()>;
  using Factory = std::function<Pull()>;

  explicit Flow(Factory factory) : factory_(std::move(factory)) {}

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

inline Value make_flow(value::Flow::Factory factory) {
  return Value::make_flow(std::make_shared<value::Flow>(std::move(factory)));
}

// Defers `start` (which consumes the upstream) to the first pull.
inline Pull lazy(std::function<Pull()> start) {
  auto inner = std::make_shared<std::optional<Pull>>();
  return [start = std::move(start), inner]() -> std::optional<Value> {
    if (!inner->has_value()) *inner = start();
    return (**inner)();
  };
}

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

inline value::Flow& require_flow(const Value& source) {
  if (source.kind != value::Kind::Flow) throw value::UnsupportedError{};
  return *source.flow;
}

// Clamps an Integer count to int64_t. Counts beyond +-2^62 behave the same
// as unbounded/empty for any real stream.
inline int64_t count_of(const Value& count) {
  if (count.kind != value::Kind::Integer) throw value::UnsupportedError{};
  const std::string text = count.integer.to_decimal_string();
  const bool negative = !text.empty() && text[0] == '-';
  const size_t digits = negative ? text.size() - 1 : text.size();
  if (digits > 18) return negative ? -(int64_t{1} << 62) : (int64_t{1} << 62);
  return std::stoll(text);
}

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

inline Value collect(const Value& source) {
  if (source.kind == value::Kind::List) return Value::make_list(*source.list_items);
  Pull items = require_flow(source).consume();
  std::vector<Value> collected;
  while (auto item = items()) collected.push_back(std::move(*item));
  return Value::make_list(std::move(collected));
}

inline Value run(const Value& source) {
  if (source.kind != value::Kind::List) {
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
    if (!last_is_flow) throw value::UnsupportedError{};
    out = scan(args[0], args[1], args[2], invoke);
    return true;
  }
  if (name == "keep_some" && n == 1) {
    require_flow(args[0]);
    out = keep_some(args[0]);
    return true;
  }
  if (name == "each" && n == 2) {
    if (args[1].kind != value::Kind::List && !last_is_flow) throw value::UnsupportedError{};
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
  if (name == "_seq_reduce" && n == 3) {
    out = reduce(args[0], args[1], args[2], invoke);
    return true;
  }
  return false;
}

}  // namespace genia::flow
