// Host-private R25 causal work accounting shared by Cell and Process.
// No Genia API, timing promise, ownership registry or capability is introduced.
#pragma once

#include <condition_variable>
#include <cstddef>
#include <mutex>

namespace genia::value {

// Count accepted items until callback, committed dispatch, or discard finishes.
// A parent remains outstanding while admitting children, so zero is a causal
// fixed point even across entity types and newly created targets. External root
// admissions must precede await_idle; no guarantee covers unrelated future producers.
class R25Quiescence {
 public:
  // Record admission while holding the entity lock, before worker visibility.
  static void accept() {
    std::lock_guard lock(mutex_);
    ++pending_;
  }

  // Retire exactly count previously accepted items after dispatch or discard.
  // Never hold this lock while acquiring an entity lock or invoking user code.
  static void complete(std::size_t count = 1) {
    std::lock_guard lock(mutex_);
    pending_ -= count;
    if (pending_ == 0) idle_.notify_all();
  }

  // Private evidence barrier; wait without polling until causal work settles.
  // Unfinished callbacks may block forever. No worker is stopped or retained.
  static void await_idle() {
    std::unique_lock lock(mutex_);
    idle_.wait(lock, [] { return pending_ == 0; });
  }

 private:
  friend struct CellQuiescenceProbe;
  inline static std::mutex mutex_;
  inline static std::condition_variable idle_;
  inline static std::size_t pending_ = 0;
};

}  // namespace genia::value
