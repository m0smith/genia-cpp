// Canonical rendering for the E24-2/E24-3 vertical slice: the
// auto-display format command mode writes for a program's final result
// (matching genia-2026's `_emit_result`, which uses the debug/display
// representation followed by a single trailing newline). Verified
// directly against the reference host for each kind this slice
// supports: Integer/Boolean render bare (`42`, `true`); String renders
// double-quoted; List renders bracketed with ", " between elements,
// recursively rendering each element the same way (e.g.
// `[["a", 1], ["b", 2]]`).
#pragma once

#include <optional>
#include <string>

#include "value.hpp"

namespace genia::render {

// Returns std::nullopt for anything this slice cannot honestly render
// (Bytes and Map have no pinned evidence requiring a rendered form;
// Opaque never should reach a final program result) -- never a
// placeholder string.
inline std::optional<std::string> display(const value::Value& value) {
  switch (value.kind) {
    case value::Kind::Integer:
      return value.integer.to_decimal_string();
    case value::Kind::Boolean:
      return value.boolean ? "true" : "false";
    case value::Kind::String: {
      std::string quoted = "\"";
      for (const char c : value.text) {
        if (c == '"' || c == '\\') {
          quoted.push_back('\\');
        }
        quoted.push_back(c);
      }
      quoted.push_back('"');
      return quoted;
    }
    case value::Kind::List: {
      std::string rendered = "[";
      bool first = true;
      for (const auto& item : *value.list_items) {
        auto item_rendered = display(item);
        if (!item_rendered.has_value()) {
          return std::nullopt;
        }
        if (!first) {
          rendered += ", ";
        }
        first = false;
        rendered += *item_rendered;
      }
      rendered += "]";
      return rendered;
    }
    case value::Kind::Bytes:
    case value::Kind::Map:
    case value::Kind::Opaque:
      return std::nullopt;
  }
  return std::nullopt;
}

}  // namespace genia::render
