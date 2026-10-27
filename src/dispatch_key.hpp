// Local R20 duplicate keys for the pattern subset this host can parse.
// Keys exclude source locations and bodies and alpha-normalize binders by
// first occurrence, preserving repeated-binding identity. Guards/varargs are
// outside this grammar and are never approximated here. Length-prefixed text
// fields prevent collisions without using protocol JSON inside the runtime.
#pragma once

#include <string>
#include <unordered_map>

#include "pattern.hpp"

namespace genia::dispatch_key {

// Appends a length-delimited field, preserving arbitrary string/key contents.
inline void append_field(std::string& key, const std::string& field) {
  key += std::to_string(field.size()) + ":" + field;
}

// Builds a structural key using one binder table per clause. Recursive calls
// share the table so [x,x] differs from [x,y], while [x,x] equals [y,y]. Integer
// spellings normalize leading zeros; literal kinds remain structurally distinct.
// Input patterns are unchanged; storage is bounded by pattern and binder size.
inline std::string pattern_key(const pattern::Pattern& pattern,
                               std::unordered_map<std::string, size_t>& binders) {
  std::string key = std::to_string(static_cast<int>(pattern.kind)) + ":";
  if (pattern.kind == pattern::Kind::Bind || pattern.kind == pattern::Kind::Rest) {
    if (pattern.name.empty()) {
      append_field(key, "");
    } else {
      const auto [entry, inserted] = binders.emplace(pattern.name, binders.size());
      (void)inserted;
      append_field(key, std::to_string(entry->second));
    }
  } else if (pattern.kind == pattern::Kind::Literal) {
    key += pattern.literal_is_decimal ? "D" : pattern.literal_is_string ? "S" : "I";
    auto name = pattern.name;
    if (!pattern.literal_is_decimal && !pattern.literal_is_string) {
      const auto first = name.find_first_not_of('0');
      name = first == std::string::npos ? "0" : name.substr(first);
    }
    append_field(key, name);
    append_field(key, pattern.decimal_coefficient_digits);
    append_field(key, std::to_string(pattern.decimal_exponent));
  }
  append_field(key, std::to_string(pattern.items.size()));
  for (const auto& item : pattern.items) append_field(key, pattern_key(item, binders));
  append_field(key, std::to_string(pattern.map_items.size()));
  for (const auto& [name, item] : pattern.map_items) {
    append_field(key, name);
    append_field(key, pattern_key(item, binders));
  }
  return key;
}

// Returns the span-free fixed-shape key. A non-tuple single-argument pattern
// normalizes to the same tuple form as an explicit one-position header.
inline std::string clause_key(const pattern::Pattern& pattern) {
  std::unordered_map<std::string, size_t> binders;
  const auto tuple =
      pattern.kind == pattern::Kind::Tuple ? pattern : pattern::Pattern::tuple({pattern});
  return pattern_key(tuple, binders);
}

}  // namespace genia::dispatch_key
