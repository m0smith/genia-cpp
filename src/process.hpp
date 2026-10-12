// Bounded C++ R25 local Process realization: a shared-identity handle owns a
// FIFO mailbox and one handler worker. Native failure/admission observations
// are bridged by the evaluator; thread/registry choices are host-local, not
// portable Actor, scheduler, supervision or distributed-message semantics.
#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "cell.hpp"

namespace genia::value {

// Own a handler, queued Values and a worker until the last shared handle dies.
// Handler failure permanently closes admission and discards queued messages.
// Avoid ownership cycles through captures/messages; destruction joins the worker.
class Process : public std::enable_shared_from_this<Process> {
 public:
  // A present result means handled (its Value is ignored); nullopt or exception
  // fails the process. Value-level Outcomes are still present Values, not exceptions.
  using Handler = std::function<std::optional<Value>(const Value&)>;

  // Move the handler into a new process owning its worker; register a weak handle.
  // Allocation/thread-start failures propagate; no message is invoked at creation.
  static std::shared_ptr<Process> create(Handler handler) {
    auto process = std::shared_ptr<Process>(new Process(std::move(handler)));
    // Serialize weak registration without retaining process lifetime.
    std::lock_guard lock(registry_mutex());
    registry().push_back(process);
    return process;
  }

  // Request termination and join: queued messages need not be drained and an
  // active handler is not interrupted. Join may block forever if the handler does.
  // The last owner must release off-worker to avoid a self-join.
  ~Process() {
    {
      // Protect termination state before notifying the owned worker.
      std::lock_guard lock(mutex_);
      terminate_ = true;
    }
    condition_.notify_all();
    if (worker_.joinable()) worker_.join();
  }

  // Move a message into FIFO admission order, or return false after failure.
  // True means queued, not handled; there is no capacity/backpressure bound.
  // Queue allocation failures propagate and no retry/restart is supplied.
  bool send(Value message) {
    // Serialize admission with worker failure and mailbox accounting.
    std::lock_guard lock(mutex_);
    if (failed_) return false;
    messages_.push_back(std::move(message));
    ++accepted_;
    condition_.notify_all();
    return true;
  }

  // Snapshot open admission (!failed), not std::thread liveness: a failed worker
  // remains waiting in this realization until destruction requests termination.
  bool alive() const {
    // Observe admission state consistently with worker failure.
    std::lock_guard lock(mutex_);
    return !failed_;
  }
  // Snapshot permanent handler-failure state without waiting for queued messages.
  bool failed() const {
    // Observe failure state consistently with worker completion.
    std::lock_guard lock(mutex_);
    return failed_;
  }
  // Copy cached "process handler failed" text, or nullopt before handler failure.
  // Raw exception text is not retained; this does not await message completion.
  std::optional<std::string> error() const {
    // Copy cached error consistently with worker failure recording.
    std::lock_guard lock(mutex_);
    return error_;
  }
  // Private evidence wait: block without timeout until accepted messages are
  // handled/discarded. Counter completion precedes staged-action execution, so
  // this is not a nested-send commit barrier or global quiescence guarantee.
  void await_idle() const {
    // Release the mutex during idle waiting and recheck the counter predicate.
    std::unique_lock lock(mutex_);
    idle_.wait(lock, [this] { return accepted_ == completed_; });
  }

  // Private evidence helper: prune the weak registry and await one owning
  // snapshot. Later registrations/sends/staged actions may race it; no global
  // quiescence or repeated counter sweep is promised by this Process helper.
  static void await_all_idle() {
    std::vector<std::shared_ptr<Process>> snapshot;
    {
      // Protect weak entry pruning and temporary snapshot ownership acquisition.
      std::lock_guard lock(registry_mutex());
      auto& entries = registry();
      for (auto it = entries.begin(); it != entries.end();) {
        if (auto process = it->lock()) {
          snapshot.push_back(std::move(process));
          ++it;
        } else {
          it = entries.erase(it);
        }
      }
    }
    for (const auto& process : snapshot) process->await_idle();
  }

 private:
  // Own the handler and start the worker; create establishes shared ownership
  // and weak registration. Worker invocation begins only after a message arrives.
  explicit Process(Handler handler) : handler_(std::move(handler)), worker_([this] { run(); }) {}

  // Consume FIFO messages one at a time outside the process lock. Stage nested
  // sends thread-locally; nullopt/handler exception clears queued work, records
  // stable failure text and discards that invocation's staged actions. Success
  // accounts for the message before invoking staged actions outside the lock.
  // Staged-action exceptions are uncaught here; handlers are not cancelled and
  // failure does not roll back other effects already performed by the handler.
  void run() {
    for (;;) {
      Value message;
      {
        // Release the mutex while waiting, then own dequeue/termination checks.
        std::unique_lock lock(mutex_);
        condition_.wait(lock, [this] { return terminate_ || !messages_.empty(); });
        if (terminate_) return;
        message = std::move(messages_.front());
        messages_.pop_front();
      }
      std::vector<std::function<void()>> staged;
      g_cell_staged_sends = &staged;
      std::optional<Value> result;
      try {
        result = handler_(message);
      } catch (...) {
        result.reset();
      }
      g_cell_staged_sends = nullptr;
      {
        // Commit completion or failure/queue discard atomically with admission.
        std::lock_guard lock(mutex_);
        if (!result.has_value()) {
          failed_ = true;
          error_ = "process handler failed";
          completed_ += 1 + messages_.size();
          messages_.clear();
        } else {
          ++completed_;
        }
        idle_.notify_all();
      }
      if (result.has_value())
        for (auto& action : staged) action();
    }
  }

  // Borrow the process-local weak registry; all access requires registry_mutex.
  // Entries never keep the process or its worker alive.
  static std::vector<std::weak_ptr<Process>>& registry() {
    static std::vector<std::weak_ptr<Process>> processes;
    return processes;
  }
  // Borrow the process-local mutex protecting weak registry mutation/traversal.
  static std::mutex& registry_mutex() {
    static std::mutex mutex;
    return mutex;
  }

  Handler handler_;
  mutable std::mutex mutex_;
  mutable std::condition_variable idle_;
  std::condition_variable condition_;
  std::deque<Value> messages_;
  bool failed_ = false;
  bool terminate_ = false;
  std::optional<std::string> error_;
  std::size_t accepted_ = 0;
  std::size_t completed_ = 0;
  std::thread worker_;
};

}  // namespace genia::value
