// C++ R25 Cell realization: FIFO worker updates over a shared Ref, generation
// isolation on restart, and transaction-local staging of nested sends.
// These native helpers use bool/optional sentinels; the evaluator supplies the
// Genia-facing failure boundary. Thread/lock/registry choices are host-local.
#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "r25_quiescence.hpp"
#include "ref.hpp"

namespace genia::value {

// Borrowed transaction buffer, valid only during the active worker callback.
inline thread_local std::vector<std::function<void()>>* g_cell_staged_sends = nullptr;
// Private evidence-fixture switch; ordinary evaluation leaves it disabled.
inline thread_local bool g_r25_fixture_enabled = false;

// Move an action into the active worker transaction, returning true if staged.
// With no transaction return false and leave execution to the caller. Allocation
// errors propagate; this helper does not invoke the action. Target lifetime
// follows the action captures held by the transaction buffer.
inline bool stage_cell_send(std::function<void()> action) {
  if (g_cell_staged_sends == nullptr) return false;
  g_cell_staged_sends->push_back(std::move(action));
  return true;
}

// Shared-identity asynchronous cell owning one worker and a shared backing Ref.
// Accepted updates run FIFO; absent/throwing replacements fail the current
// generation without overwriting its last committed state. Destroying the cell
// joins its worker, so callbacks must finish and ownership cycles must be avoided.
class Cell : public std::enable_shared_from_this<Cell> {
 public:
  // Admission state, not native-thread liveness: stopped/failed workers still wait.
  enum class Status { Ready, Stopped, Failed };
  // Return replacement state, or nullopt for failure; exceptions also fail work.
  using Update = std::function<std::optional<Value>(const Value&)>;

  // Allocate an initialized Ref and start a cell owning that shared state.
  // Allocation/thread-start failures propagate; the caller owns the handle.
  static std::shared_ptr<Cell> create(Value initial) {
    return create_with_state(std::make_shared<Ref>(std::move(initial)));
  }

  // Start a cell sharing the supplied non-null Ref without copying its identity.
  // An unset Ref blocks state reads and the worker until set. Construction may throw.
  static std::shared_ptr<Cell> create_with_state(std::shared_ptr<Ref> state) {
    auto cell = std::shared_ptr<Cell>(new Cell(std::move(state)));
    return cell;
  }

  // Request worker termination and join it; queued work need not be drained.
  // An active callback is not interrupted, so destruction can block indefinitely.
  // Destruction must occur off the owned worker to avoid self-join.
  ~Cell() {
    {
      // Protect termination state before waking the worker.
      std::lock_guard lock(mutex_);
      terminate_ = true;
    }
    condition_.notify_all();
    if (worker_.joinable()) worker_.join();
    R25Quiescence::complete(queue_.size());
  }

  // Enqueue one update in admission order, returning false if stopped or failed.
  // True means accepted, not completed; the worker owns the moved callback.
  // Queue allocation failures propagate and there is no capacity/backpressure bound.
  bool send(Update update) {
    // Serialize this admission/state operation with worker commits and restart.
    std::lock_guard lock(mutex_);
    if (status_ != Status::Ready) return false;
    queue_.push_back(Work{generation_, std::move(update)});
    ++accepted_;
    R25Quiescence::accept();
    condition_.notify_all();
    return true;
  }

  // Copy the current Ref value, or return nullopt when this cell has failed.
  // This holds the cell lock while reading the Ref and may block if it is unset.
  // Stopped cells remain readable; this does not await queued updates.
  std::optional<Value> get() const {
    // Serialize this admission/state operation with worker commits and restart.
    std::lock_guard lock(mutex_);
    if (status_ == Status::Failed) return std::nullopt;
    return state_->get();
  }

  // Return the shared Ref identity, including after failure; no state read or wait.
  std::shared_ptr<Ref> backing_state() const { return state_; }
  // Snapshot synchronized admission state without waiting for pending updates.
  Status status() const {
    // Serialize this admission/state operation with worker commits and restart.
    std::lock_guard lock(mutex_);
    return status_;
  }
  // Copy cached "cell update failed" text; absent before failure or after restart.
  std::optional<std::string> error() const {
    // Serialize this admission/state operation with worker commits and restart.
    std::lock_guard lock(mutex_);
    return error_;
  }

  // Change Ready to Stopped immediately, rejecting new work but retaining queued
  // work for execution. Failed/stopped calls are no-ops; this does not join the worker.
  void stop() {
    // Serialize this admission/state operation with worker commits and restart.
    std::lock_guard lock(mutex_);
    if (status_ == Status::Ready) status_ = Status::Stopped;
    condition_.notify_all();
  }

  // Install replacement state and reopen admission on the same shared cell.
  // Discard queued old work and invalidate callbacks checked after restart; their
  // results cannot commit state or staged sends. Already-committed staged actions
  // may still execute outside the lock. Clear cached failure.
  // The owned worker is reused; this is not a general supervision/retry policy.
  std::shared_ptr<Cell> restart(Value new_state) {
    // Serialize this admission/state operation with worker commits and restart.
    std::lock_guard lock(mutex_);
    ++generation_;
    completed_ += queue_.size();
    R25Quiescence::complete(queue_.size());
    queue_.clear();
    state_->set(std::move(new_state));
    status_ = Status::Ready;
    error_.reset();
    idle_.notify_all();
    condition_.notify_all();
    return shared_from_this();
  }

  // Wait without timeout for accepted work to be accounted for as completed or
  // discarded, including committed staged dispatch. Callbacks/actions may block
  // forever. Target work itself is joined by the shared causal evidence barrier.
  // C++ API: do not call from this entity's own callback or staged action; its
  // outstanding work cannot retire until that callback/action finishes, so the
  // wait cannot return. Genia staged actions are send-only.
  void await_idle() const {
    // Release the cell mutex while waiting; recheck counters on every wake.
    std::unique_lock lock(mutex_);
    idle_.wait(lock, [this] { return accepted_ == completed_; });
  }

  // Private evidence barrier shared with Process: includes all causal targets,
  // including entities created by callbacks. No unrelated future producer join.
  // C++ API: do not call from any entity's own callback or staged action; its
  // outstanding work cannot retire until that callback/action finishes, so the
  // shared wait cannot return. Genia staged actions are send-only.
  static void await_all_idle() { R25Quiescence::await_idle(); }

 private:
  friend struct CellQuiescenceProbe;
  // Queue-owned update paired with its acceptance generation for stale rejection.
  struct Work {
    std::uint64_t generation;
    Update update;
  };

  // Own the non-null backing Ref and start the worker; use create_with_state
  // so shared_from_this is established for later restart.
  explicit Cell(std::shared_ptr<Ref> state)
      : state_(std::move(state)), worker_([this] { run(); }) {}

  // Consume FIFO work outside the cell lock, staging nested sends thread-locally.
  // Nullopt or callback exception fails only current-generation work, clears its
  // queue and preserves state; stale results are discarded even on failure.
  // Commit state under lock, dispatch outside it, then complete the accepted item.
  // Staged-action exceptions are not caught here and callbacks are not cancelled.
  void run() {
    for (;;) {
      Work work;
      {
        // Release the cell mutex while waiting for work or termination.
        std::unique_lock lock(mutex_);
        condition_.wait(lock, [this] { return terminate_ || !queue_.empty(); });
        if (terminate_) return;
        work = std::move(queue_.front());
        queue_.pop_front();
      }
      std::vector<std::function<void()>> staged;
      g_cell_staged_sends = &staged;
      std::optional<Value> replacement;
      try {
        replacement = work.update(state_->get());
      } catch (...) {
        replacement.reset();
      }
      g_cell_staged_sends = nullptr;
      bool commit_staged = false;
      {
        // Atomically account for this generation and commit its state or failure.
        std::lock_guard lock(mutex_);
        if (work.generation != generation_) {
          ++completed_;
          R25Quiescence::complete();
        } else if (!replacement.has_value()) {
          status_ = Status::Failed;
          error_ = "cell update failed";
          completed_ += 1 + queue_.size();
          R25Quiescence::complete(1 + queue_.size());
          queue_.clear();
        } else {
          state_->set(std::move(*replacement));
          commit_staged = true;
        }
        idle_.notify_all();
      }
      if (commit_staged) {
        for (auto& action : staged) action();
        // Retire the parent only after all committed child admissions finish.
        std::lock_guard lock(mutex_);
        ++completed_;
        R25Quiescence::complete();
        idle_.notify_all();
      }
    }
  }

  std::shared_ptr<Ref> state_;
  mutable std::mutex mutex_;
  mutable std::condition_variable idle_;
  std::condition_variable condition_;
  std::deque<Work> queue_;
  Status status_ = Status::Ready;
  std::optional<std::string> error_;
  std::uint64_t generation_ = 0;
  std::size_t accepted_ = 0;
  std::size_t completed_ = 0;
  bool terminate_ = false;
  std::thread worker_;
};

}  // namespace genia::value
