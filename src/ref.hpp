// C++ R25 Ref realization: synchronized optional storage with blocking reads
// and serialized callback replacement. Native mutex/condition choices do not
// define Genia semantics; callers retain ownership for the duration of an operation.
#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <utility>

#include "value.hpp"

namespace genia::value {

// Shared through opaque Genia Ref values; copies of a handle retain entity identity.
// Stored Value copies preserve their own nested handle ownership, not deep snapshots.
// No timeout, fairness or same-Ref callback re-entry guarantee is provided.
class Ref {
 public:
  // Construct unset storage; get/update wait until a setter supplies a value.
  Ref() = default;
  // Construct set storage owning the initial Value, including nested handles.
  explicit Ref(Value initial) : value_(std::move(initial)) {}

  // Block without timeout until set, then copy the value under the Ref lock.
  // Copy/allocation exceptions propagate; this does not consume or unset storage.
  Value get() const {
    // Release the mutex during the unset wait; protect the subsequent value access.
    std::unique_lock lock(mutex_);
    condition_.wait(lock, [this] { return value_.has_value(); });
    return *value_;
  }

  // Replace storage under lock, wake blocked readers/updaters, and return the
  // supplied Value copy. This also initializes an unset Ref; copy failures propagate.
  Value set(Value replacement) {
    Value result = replacement;
    {
      // Serialize replacement with reads and the entire locked update callback.
      std::lock_guard lock(mutex_);
      value_ = std::move(replacement);
    }
    condition_.notify_all();
    return result;
  }

  // Snapshot presence without waiting or observing the stored Value contents.
  bool is_set() const {
    // Read presence consistently with setter/update writes.
    std::lock_guard lock(mutex_);
    return value_.has_value();
  }

  // Wait until set, invoke updater once under the exclusive lock, then store and
  // return its replacement. Keep callbacks fast and do not re-enter this Ref:
  // the mutex is non-recursive. Callback exceptions propagate without replacement;
  // any external effects already performed by the callback are not rolled back.
  Value update(const std::function<Value(const Value&)>& updater) {
    // Release while awaiting initialization, then hold throughout updater and commit.
    std::unique_lock lock(mutex_);
    condition_.wait(lock, [this] { return value_.has_value(); });
    Value replacement = updater(*value_);
    value_ = replacement;
    lock.unlock();
    condition_.notify_all();
    return replacement;
  }

 private:
  mutable std::mutex mutex_;
  mutable std::condition_variable condition_;
  std::optional<Value> value_;
};

}  // namespace genia::value
