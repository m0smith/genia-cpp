// Canonical rendering for the E24-2..E24-4 vertical slice: the
// auto-display format command mode writes for a program's final result
// (matching genia-2026's `_emit_result`, which uses the debug/display
// representation followed by a single trailing newline). Verified
// directly against the reference host for each kind this slice
// supports: Integer/Boolean render bare (`42`, `true`); String renders
// double-quoted; List renders bracketed with ", " between elements,
// recursively rendering each element the same way (e.g.
// `[["a", 1], ["b", 2]]`).
#pragma once

#include <cctype>
#include <optional>
#include <string>

#include "value.hpp"

namespace genia::render {

inline std::optional<std::string> display(const value::Value& value);

// A map-literal key renders bare when it is a legal Genia identifier
// (matching genia-2026's src/genia/utf8.py `_format_map_key`'s
// `_GENIA_IDENT_RE` check -- this slice's map-literal keys are always
// produced from a bare identifier or plain string, which already
// satisfies that pattern whenever the string looks identifier-shaped),
// otherwise quoted via the ordinary string debug rendering.
inline bool looks_like_identifier(const std::string& text) {
  if (text.empty()) {
    return false;
  }
  const unsigned char first = static_cast<unsigned char>(text[0]);
  if (!(std::isalpha(first) || first == '_' || first == '$')) {
    return false;
  }
  for (char c : text) {
    const unsigned char uc = static_cast<unsigned char>(c);
    if (!(std::isalnum(uc) || c == '_' || c == '?' || c == '!' || c == '.' || c == '-' ||
          c == '$')) {
      return false;
    }
  }
  return true;
}

// Returns std::nullopt for anything this slice cannot honestly render
// (Bytes and Closure have no pinned evidence requiring a rendered form;
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
    case value::Kind::Map: {
      std::string rendered = "{";
      bool first = true;
      for (const auto& [key, mapped_value] : value.map->items()) {
        auto value_rendered = display(mapped_value);
        if (!value_rendered.has_value()) {
          return std::nullopt;
        }
        if (!first) {
          rendered += ", ";
        }
        first = false;
        // This slice's map-literal keys are always Kind::String (see
        // core_ir.hpp's IrMap comment); a key of any other kind has no
        // pinned evidence and is honestly unsupported.
        if (key.kind != value::Kind::String) {
          return std::nullopt;
        }
        if (looks_like_identifier(key.text)) {
          rendered += key.text;
        } else {
          auto key_rendered = display(key);
          if (!key_rendered.has_value()) {
            return std::nullopt;
          }
          rendered += *key_rendered;
        }
        rendered += ": ";
        rendered += *value_rendered;
      }
      rendered += "}";
      return rendered;
    }
    case value::Kind::Outcome: {
      auto reason_rendered = display(*value.outcome_reason);
      if (!reason_rendered.has_value()) {
        return std::nullopt;
      }
      std::string rendered = "err(" + *reason_rendered;
      if (value.outcome_context != nullptr) {
        auto context_rendered = display(*value.outcome_context);
        if (!context_rendered.has_value()) {
          return std::nullopt;
        }
        rendered += ", " + *context_rendered;
      }
      rendered += ")";
      return rendered;
    }
    case value::Kind::Bytes:
    case value::Kind::Closure:
    case value::Kind::Opaque:
      return std::nullopt;
  }
  return std::nullopt;
}

}  // namespace genia::render
