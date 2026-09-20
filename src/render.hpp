// Canonical rendering for the E24-2 vertical slice: the auto-display
// format command mode writes for a program's final result (matching
// genia-2026's `_emit_result`, which uses the debug/display
// representation followed by a single trailing newline -- for a plain
// Integer, debug and display renderings coincide: the canonical decimal
// digits, sign included, no quoting).
#pragma once

#include <optional>
#include <string>

#include "value.hpp"

namespace genia::render {

// Returns std::nullopt for anything this slice cannot honestly render
// (only Kind::Opaque at this slice) -- never a placeholder string.
inline std::optional<std::string> display(const value::Value& value) {
  if (value.kind != value::Kind::Integer) {
    return std::nullopt;
  }
  return value.integer.to_decimal_string();
}

}  // namespace genia::render
