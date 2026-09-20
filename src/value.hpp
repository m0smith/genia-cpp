// Runtime values for the E24-2/E24-3 vertical slice.
//
// Kinds: exact Integer, Boolean, String, Bytes (from utf8_encode),
// List, Map (the native in-house insertion-ordered map), and an opaque
// placeholder for a known-but-not-yet-callable global binding (see
// global_env.hpp). Decimal/Rational/Float64/lambdas/Outcomes are all
// later slices' scope (E24-4/E24-7) -- this is deliberately not a
// general Genia value representation yet.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "bignum.hpp"

namespace genia::value {

enum class Kind : std::uint8_t { Integer, Boolean, String, Bytes, List, Map, Opaque };

class OrderedMap;

struct Value {
  Kind kind = Kind::Opaque;
  bignum::Integer integer;  // valid when kind == Integer
  bool boolean = false;     // valid when kind == Boolean
  std::string text;         // valid when kind == String or Bytes (raw UTF-8/byte content)
  std::shared_ptr<std::vector<Value>> list_items;  // valid when kind == List
  std::shared_ptr<OrderedMap> map;                 // valid when kind == Map

  static Value make_integer(bignum::Integer v) {
    Value value;
    value.kind = Kind::Integer;
    value.integer = std::move(v);
    return value;
  }

  static Value make_boolean(bool v) {
    Value value;
    value.kind = Kind::Boolean;
    value.boolean = v;
    return value;
  }

  static Value make_string(std::string v) {
    Value value;
    value.kind = Kind::String;
    value.text = std::move(v);
    return value;
  }

  static Value make_bytes(std::string v) {
    Value value;
    value.kind = Kind::Bytes;
    value.text = std::move(v);
    return value;
  }

  static Value make_list(std::vector<Value> items) {
    Value value;
    value.kind = Kind::List;
    value.list_items = std::make_shared<std::vector<Value>>(std::move(items));
    return value;
  }

  static Value make_map(std::shared_ptr<OrderedMap> m) {
    Value value;
    value.kind = Kind::Map;
    value.map = std::move(m);
    return value;
  }

  static Value make_opaque() {
    Value value;
    value.kind = Kind::Opaque;
    return value;
  }
};

// The native in-house insertion-ordered map primitive (per
// docs/design/r24/dependency-toolchain-policy.md: a vector of pairs
// plus a hash index, never std::map/std::unordered_map, which do not
// preserve insertion order). Key legality/equivalence follows R18
// exactly (docs/design/r18-portable-value-equality-contract.md "Legal
// map keys and key equivalence"): key identity is exactly Genia `==`,
// so lookup never falls back to a host container's own key rules --
// see equality.hpp's `map_key_encoding`, which this type uses
// exclusively for its hash index.
class OrderedMap {
 public:
  // Returns the index of an existing entry for `key_encoding`, or
  // appends a fresh entry and returns its index.
  size_t put(const std::string& key_encoding, Value key, Value mapped_value) {
    auto it = index_.find(key_encoding);
    if (it != index_.end()) {
      entries_[it->second].second = std::move(mapped_value);
      return it->second;
    }
    const size_t new_index = entries_.size();
    entries_.emplace_back(std::move(key), std::move(mapped_value));
    index_.emplace(key_encoding, new_index);
    return new_index;
  }

  bool has(const std::string& key_encoding) const { return index_.count(key_encoding) > 0; }

  const Value* get(const std::string& key_encoding) const {
    auto it = index_.find(key_encoding);
    if (it == index_.end()) {
      return nullptr;
    }
    return &entries_[it->second].second;
  }

  void remove(const std::string& key_encoding) {
    auto it = index_.find(key_encoding);
    if (it == index_.end()) {
      return;
    }
    const size_t removed_index = it->second;
    entries_.erase(entries_.begin() + static_cast<long>(removed_index));
    index_.erase(it);
    for (auto& pair_entry : index_) {
      if (pair_entry.second > removed_index) {
        --pair_entry.second;
      }
    }
  }

  size_t count() const { return entries_.size(); }

  const std::vector<std::pair<Value, Value>>& items() const { return entries_; }

 private:
  std::vector<std::pair<Value, Value>> entries_;
  std::unordered_map<std::string, size_t> index_;
};

}  // namespace genia::value
