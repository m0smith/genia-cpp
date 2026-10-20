// Native scheduling tests for private R25 causal accounting, not new semantics.
#include <future>
#include <iostream>
#include <source_location>
#include <stdexcept>

#include "../src/process.hpp"

using genia::value::Cell;
using genia::value::Process;
using genia::value::stage_cell_send;
using genia::value::Value;

namespace {
// Construct a small exact Integer for native state assertions.
Value number(unsigned n) { return Value::make_integer(genia::bignum::Integer::from_u64(n)); }
}  // namespace

// Read exactly the predicate used by await_idle while dispatch is causally paused.
// This avoids timing assertions about whether a separate waiter was scheduled.
namespace genia::value {
// Test-only inspection of the local and shared wait predicates.
struct CellQuiescenceProbe {
  // Inspect the exact shared barrier predicate, without scheduling a waiter.
  static bool globally_settled() {
    // Read the shared predicate under the same mutex as the barrier.
    std::lock_guard lock(R25Quiescence::mutex_);
    return R25Quiescence::pending_ == 0;
  }

  // Wait for destruction to request termination while its worker is gated.
  // The test retains the raw address only until it releases that worker.
  static void await_termination(Cell& cell) {
    std::unique_lock lock(cell.mutex_);
    cell.condition_.wait(lock, [&] { return cell.terminate_; });
  }

  // Read the Cell local wait predicate while its committed action is gated.
  static bool settled(const Cell& cell) {
    std::lock_guard lock(cell.mutex_);
    return cell.accepted_ == cell.completed_;
  }
};
}  // namespace genia::value

namespace genia::value {
// Observe precisely the local wait predicate while dispatch is held by a promise.
struct ProcessQuiescenceProbe {
  // Read the Process local predicate while its committed action is gated.
  static bool settled(const Process& process) {
    std::lock_guard lock(process.mutex_);
    return process.accepted_ == process.completed_;
  }
};
}  // namespace genia::value

namespace {
// Main-thread-only counters for this standalone native regression executable.
unsigned assertions = 0;
unsigned failures = 0;

// Nonfatal check: a paused-action failure must still release the worker gate.
void check(bool condition, std::source_location where = std::source_location::current()) {
  ++assertions;
  if (!condition) {
    ++failures;
    std::cerr << where.function_name() << ":" << where.line() << ": check failed\n";
  }
}

// Preserve the caller location for negative predicate assertions.
void check_false(bool condition, std::source_location where = std::source_location::current()) {
  check(!condition, where);
}

// Admission prerequisites fail before a worker can be waiting on a test gate.
void require(bool condition, std::source_location where = std::source_location::current()) {
  check(condition, where);
  if (!condition) throw std::runtime_error("test admission failed");
}
}  // namespace

// R25 Process stays outstanding through every committed action.
void paused_process_dispatch() {
  std::promise<void> entered, release;
  auto gate = release.get_future().share();
  auto target = Cell::create(number(0));
  auto process = Process::create([&](const Value&) {
    stage_cell_send([&] { target->send([](const Value&) { return number(1); }); });
    stage_cell_send([&, gate] {
      entered.set_value();
      gate.wait();
      target->send([](const Value&) { return number(2); });
    });
    return std::optional<Value>(number(0));
  });
  require(process->send(number(0)));
  entered.get_future().wait();
  check_false(genia::value::ProcessQuiescenceProbe::settled(*process));
  check_false(genia::value::CellQuiescenceProbe::globally_settled());
  release.set_value();
  Process::await_all_idle();
  check(target->get()->integer.to_decimal_string() == "2");
}

// R25 causal barrier follows alternating new Cell and Process targets.
void alternating_targets() {
  // Each callback admits the next target before retiring itself. Targets
  // may be created after the wait starts; a registry snapshot cannot suffice.
  std::vector<std::shared_ptr<Cell>> cells;
  std::vector<std::shared_ptr<Process>> processes;
  auto result = std::make_shared<genia::value::Ref>(number(0));
  std::function<void(unsigned)> launch;
  launch = [&](unsigned remaining) {
    if (remaining == 0) {
      result->set(number(42));
      return;
    }
    if (remaining % 2 == 0) {
      auto cell = Cell::create(number(0));
      cells.push_back(cell);
      cell->send([&, remaining](const Value&) {
        stage_cell_send([&, remaining] { launch(remaining - 1); });
        return number(1);
      });
    } else {
      auto process = Process::create([&, remaining](const Value&) {
        stage_cell_send([&, remaining] { launch(remaining - 1); });
        return std::optional<Value>(number(1));
      });
      processes.push_back(process);
      process->send(number(0));
    }
  };
  launch(12);
  Cell::await_all_idle();
  check(result->get().integer.to_decimal_string() == "42");
  check(cells.size() == 6);
  check(processes.size() == 6);
}

// R25 stale failed and stopped generations retire exactly their work.
void stale_restart_and_stop() {
  std::promise<void> entered, release;
  auto gate = release.get_future().share();
  auto target = Cell::create(number(10));
  auto parent = Cell::create(number(0));
  require(parent->send([&, gate](const Value&) -> std::optional<Value> {
    stage_cell_send([&] { target->send([](const Value&) { return number(999); }); });
    entered.set_value();
    gate.wait();
    throw std::runtime_error("private stale error");
  }));
  entered.get_future().wait();
  require(parent->send([](const Value&) { return number(999); }));
  parent->restart(number(20));
  require(parent->send([](const Value&) { return number(21); }));
  parent->stop();
  release.set_value();
  Cell::await_all_idle();
  check(parent->get()->integer.to_decimal_string() == "21");
  check(parent->status() == Cell::Status::Stopped);
  check_false(parent->error().has_value());
  check(target->get()->integer.to_decimal_string() == "10");
}

// R25 discarded Process transaction cannot admit descendants.
void process_failure_rollback() {
  std::promise<void> entered, release;
  auto gate = release.get_future().share();
  auto target = Cell::create(number(10));
  auto process = Process::create([&, gate](const Value&) -> std::optional<Value> {
    stage_cell_send([&] { target->send([](const Value&) { return number(999); }); });
    entered.set_value();
    gate.wait();
    throw std::runtime_error("private handler error");
  });
  require(process->send(number(0)));
  entered.get_future().wait();
  require(process->send(number(1)));
  release.set_value();
  Cell::await_all_idle();
  check(process->failed());
  check(process->error() == "process handler failed");
  check_false(process->send(number(2)));
  check(target->get()->integer.to_decimal_string() == "10");
}

// R25 committed self-send runs without holding the Cell lock.
void cell_self_send() {
  auto cell = Cell::create(number(0));
  require(cell->send([&](const Value&) {
    stage_cell_send([&] { cell->send([](const Value&) { return number(2); }); });
    return number(1);
  }));
  Cell::await_all_idle();
  check(cell->get()->integer.to_decimal_string() == "2");
}

// R25 paused committed action remains outstanding.
void paused_cell_dispatch() {
  std::promise<void> entered;
  std::promise<void> release;
  auto gate = release.get_future().share();
  auto target = Cell::create(number(10));
  auto parent = Cell::create(number(0));
  require(parent->send([&](const Value&) {
    genia::value::stage_cell_send([&, gate] {
      entered.set_value();
      gate.wait();
      target->send([](const Value&) { return std::optional<Value>(number(15)); });
    });
    return std::optional<Value>(number(1));
  }));
  entered.get_future().wait();
  check(parent->get()->integer.to_decimal_string() == "1");
  check(target->get()->integer.to_decimal_string() == "10");
  check_false(genia::value::CellQuiescenceProbe::settled(*parent));
  check_false(genia::value::CellQuiescenceProbe::globally_settled());
  release.set_value();
  Cell::await_all_idle();
  check(target->get()->integer.to_decimal_string() == "15");
}

// R25 destruction retires queued work after joining the active worker.
void destruction_retires_queue() {
  std::promise<void> entered, release;
  auto gate = release.get_future().share();
  auto cell = Cell::create(number(0));
  require(cell->send([&, gate](const Value&) {
    entered.set_value();
    gate.wait();
    return number(1);
  }));
  entered.get_future().wait();
  bool queued_ran = false;
  require(cell->send([&](const Value&) {
    queued_ran = true;
    return number(99);
  }));
  auto* address = cell.get();
  std::thread destroyer([owner = std::move(cell)]() mutable { owner.reset(); });
  genia::value::CellQuiescenceProbe::await_termination(*address);
  check_false(genia::value::CellQuiescenceProbe::globally_settled());
  release.set_value();
  destroyer.join();
  Cell::await_all_idle();
  check_false(queued_ran);
  check(genia::value::CellQuiescenceProbe::globally_settled());
}

// Exercise the adversarial schedules; CTest supplies the hang guard, not semantics.
int main() {
  try {
    paused_process_dispatch();
    alternating_targets();
    stale_restart_and_stop();
    process_failure_rollback();
    cell_self_send();
    paused_cell_dispatch();
    destruction_retires_queue();
  } catch (const std::exception& error) {
    std::cerr << error.what() << "\n";
    return 1;
  }
  std::cout << "7 quiescence schedules, " << assertions << " assertions, " << failures
            << " failures\n";
  return failures == 0 ? 0 : 1;
}
