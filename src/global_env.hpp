// Global environment contents for the E24-2..E24-4 vertical slice.
//
// `print` is bound (by evaluator.hpp's `eval_program`) to an opaque
// placeholder value: referencing it succeeds (so this one real global
// name resolves instead of raising an undefined-name error), but
// nothing in this slice can call it, render it, or otherwise observe
// more than "a value exists" -- verified directly against
// spec/cli/command_mode_basic.yaml's `print 123`, which genia-2026's
// own parser AST confirms lowers to two independent top-level
// statements (a bare `Var("print")` reference whose value is never
// used, followed by the literal `123`), never an actual call to
// `print`.
//
// `kPreludeSource` is a small, genuinely-Genia-source-level
// installation of exactly the prelude functions E24-4's pinned evidence
// needs that are not trivial 1:1 native pass-throughs (see
// native_functions.hpp's header comment for the trivial-wrapper cases,
// and docs/design/r24/native-primitive-inventory.md's "Explicitly NOT
// native" section: "List/map prelude helpers ... interpreted from the
// shared Genia prelude source, same as every other host"). It is parsed,
// lowered, and evaluated by evaluator.hpp's `eval_program` through the
// exact same real pipeline as user source -- never a native C++
// reimplementation of `map`'s dispatch/recursion.
//
// Copied verbatim (function signatures and bodies) from genia-2026's
// real prelude source, with only the `@doc`/`@category` prefix
// annotations stripped -- those are pure documentation metadata with no
// evaluator-consulted runtime effect (verified: src/genia/evaluator.py
// never inspects an IrAnnotation's content for `@doc`/`@category`,
// beyond storing it as binding metadata nothing here reads), so omitting
// them changes nothing observable:
//   - `sum(xs) = _sum(xs)`: src/genia/std/prelude/math.genia, line 46.
//   - `map(f, xs) = map_acc(f, xs, [])` and `map_acc`'s three case
//     clauses: src/genia/std/prelude/list.genia, lines 132-137.
//
// `err(reason[, context])` is deliberately NOT sourced this way. Its
// real prelude definition is `err(..args) = _err(..args)` -- a trivial
// pass-through, exactly like E24-3's map_new/map_put precedent, except
// its wrapper needs a variadic (`..args`) rest parameter this slice does
// not otherwise implement (no pinned evidence needs general varargs
// functions). Per that same precedent, a trivial wrapper that adds no
// logic beyond argument pass-through may be implemented as a native C++
// callable directly (see native_functions.hpp's "err" case) rather than
// justifying a new grammar feature to parse it.
#pragma once

#include <string>

namespace genia::global_env {

inline const std::string& prelude_source() {
  static const std::string kPreludeSource =
      "sum(xs) = _sum(xs)\n"
      "map(f, xs) = map_acc(f, xs, [])\n"
      "map_acc(f, xs, acc) = (f, [], acc) -> acc | (f, [x, ..rest], acc) -> "
      "map_acc(f, rest, [..acc, apply_raw(f, [x])]) | (f, xs, _) -> _seq_type_error(\"map\", xs)\n";
  return kPreludeSource;
}

}  // namespace genia::global_env
