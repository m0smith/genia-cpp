// Minimal global environment for the E24-2 vertical slice.
//
// This is NOT prelude autoloading (that capability, `prelude_autoload`,
// remains unimplemented -- see docs/design/r24/capability-floor.json in
// genia-2026). It binds exactly the one real, documented Genia global
// name this slice's pinned evidence references
// (spec/cli/command_mode_basic.yaml's `print 123`, which genia-2026's
// own parser AST confirms lowers to two independent top-level
// statements -- a bare `Var("print")` reference whose value is never
// used, followed by the literal `123` -- never an actual call to
// `print`; verified directly against genia-2026's reference host).
//
// `print` is bound to an opaque placeholder value: referencing it
// succeeds (so this one real global name resolves instead of raising an
// undefined-name error), but nothing in this slice can call it, render
// it, or otherwise observe more than "a value exists". Any other name
// is genuinely undefined at this slice -- normalizing that into a real
// R19 diagnostic is E24-4's `deterministic_runtime_error_behavior`
// scope, so an unknown name here must be reported as this whole
// eval/lower/parse case being unsupported, never guessed at.
#pragma once

#include <optional>
#include <string>
#include <unordered_map>

#include "value.hpp"

namespace genia::global_env {

inline std::optional<value::Value> lookup(const std::string& name) {
  static const std::unordered_map<std::string, value::Value> kGlobals = {
      {"print", value::Value::make_opaque()},
  };
  auto it = kGlobals.find(name);
  if (it == kGlobals.end()) {
    return std::nullopt;
  }
  return it->second;
}

}  // namespace genia::global_env
