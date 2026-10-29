// Local R20 duplicate keys for the pattern subset this host can parse.
// Keys exclude source locations and bodies and alpha-normalize binders by
// first occurrence, preserving repeated-binding identity. Guard references use
// those binder slots; lambda scopes shadow them. Varargs remain unsupported.
// Length-prefixed text
// fields prevent collisions without using protocol JSON inside the runtime.
#pragma once

#include <algorithm>
#include <string>
#include <unordered_map>

#include "core_ir.hpp"
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

// Give lambda binders fresh slots, including names shadowing outer bindings.
// Repeated names within one lambda reuse one slot; the caller owns the scope.
inline void bind_lambda_pattern(const pattern::Pattern& pattern,
                                std::unordered_map<std::string, size_t>& scope,
                                std::unordered_map<std::string, size_t>& local, size_t& next) {
  if ((pattern.kind == pattern::Kind::Bind || pattern.kind == pattern::Kind::Rest) &&
      !pattern.name.empty()) {
    const auto [entry, inserted] = local.emplace(pattern.name, next);
    if (inserted) ++next;
    scope[pattern.name] = entry->second;
  }
  for (const auto& item : pattern.items) bind_lambda_pattern(item, scope, local, next);
  for (const auto& item : pattern.map_items) bind_lambda_pattern(item.value, scope, local, next);
}

// Span-free expression structure for guard syntax accepted by this host. Bound
// references share pattern slots; free names stay literal. Quote contents and
// named-access keys are data, not alpha-normalized references. No evaluation,
// rendering, metadata or source locations participate in the key.
inline std::string expression_key(const core_ir::Node& node,
                                  const std::unordered_map<std::string, size_t>& binders) {
  std::string key = std::to_string(static_cast<int>(node.kind)) + ":";
  auto reference = [&](const std::string& name) {
    auto found = binders.find(name);
    return found == binders.end() ? "free:" + name : "bound:" + std::to_string(found->second);
  };
  if (node.kind == core_ir::Kind::Literal) {
    append_field(key, std::to_string(static_cast<int>(node.literal_kind)));
    const auto first = node.integer_digits.find_first_not_of('0');
    append_field(key, first == std::string::npos ? "0" : node.integer_digits.substr(first));
    append_field(key, node.decimal_coefficient_digits);
    append_field(key, std::to_string(node.decimal_exponent));
    append_field(key, node.string_value);
    append_field(key, node.bool_value ? "true" : "false");
  }
  if (node.kind == core_ir::Kind::Var || node.kind == core_ir::Kind::Call)
    append_field(key, reference(node.name));
  if (node.kind == core_ir::Kind::Binary || node.kind == core_ir::Kind::Unary) {
    append_field(key, core_ir::op_token_name(node.op));
    append_field(key, node.named_access ? "access" : "operator");
  }
  if (node.kind == core_ir::Kind::Lambda) {
    auto scope = binders;
    std::unordered_map<std::string, size_t> local;
    size_t next = 0;
    for (const auto& entry : scope) next = std::max(next, entry.second + 1);
    for (const auto& param : node.params) bind_lambda_pattern(param, scope, local, next);
    append_field(key, std::to_string(node.params.size()));
    for (const auto& param : node.params) append_field(key, pattern_key(param, scope));
    if (node.left) append_field(key, expression_key(*node.left, scope));
    return key;
  }
  const bool quoted = node.kind == core_ir::Kind::Quote || node.kind == core_ir::Kind::QuasiQuote;
  const std::unordered_map<std::string, size_t> empty;
  if (node.left) append_field(key, expression_key(*node.left, quoted ? empty : binders));
  if (node.right)
    append_field(key, expression_key(*node.right, node.named_access ? empty : binders));
  append_field(key, std::to_string(node.items.size()));
  for (const auto& item : node.items) append_field(key, expression_key(item, binders));
  append_field(key, std::to_string(node.map_entries.size()));
  for (const auto& item : node.map_entries) {
    append_field(key, item.key);
    append_field(key, expression_key(item.value, binders));
  }
  return key;
}

// Returns the fixed-shape pattern/guard key. Missing guards remain distinct from
// explicit true guards. A scalar pattern normalizes to a one-position tuple.
inline std::string clause_key(const pattern::Pattern& pattern,
                              const std::shared_ptr<core_ir::Node>& guard = nullptr) {
  std::unordered_map<std::string, size_t> binders;
  const auto tuple =
      pattern.kind == pattern::Kind::Tuple ? pattern : pattern::Pattern::tuple({pattern});
  auto key = pattern_key(tuple, binders);
  append_field(key, guard ? "guard" : "unguarded");
  if (guard) append_field(key, expression_key(*guard, binders));
  return key;
}

}  // namespace genia::dispatch_key
