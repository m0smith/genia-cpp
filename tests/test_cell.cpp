#include <atomic>
#include <thread>

#include "../src/cell.hpp"
#include "../src/engine.hpp"
#include "../third_party/catch2/catch.hpp"

using genia::engine::try_run;
using genia::value::Cell;
using genia::value::Value;

namespace {
Value integer(std::uint64_t value) {
  return Value::make_integer(genia::bignum::Integer::from_u64(value));
}
}  // namespace

TEST_CASE("R25 Cell accepts FIFO work and preserves the last state on failure") {
  auto cell = Cell::create(integer(10));
  REQUIRE(cell->send([](const Value& value) {
    return std::optional<Value>(integer(std::stoull(value.integer.to_decimal_string()) + 2));
  }));
  REQUIRE(cell->send([](const Value& value) {
    return std::optional<Value>(integer(std::stoull(value.integer.to_decimal_string()) * 3));
  }));
  REQUIRE(cell->send([](const Value&) { return std::optional<Value>(); }));
  Cell::await_all_idle();
  CHECK(cell->backing_state()->get().integer.to_decimal_string() == "36");
  CHECK(cell->status() == Cell::Status::Failed);
  CHECK(cell->error().has_value());
}

TEST_CASE("R25 Cell restart discards an older in-flight generation") {
  auto gate = std::make_shared<genia::value::Ref>();
  auto cell = Cell::create(integer(0));
  REQUIRE(cell->send([gate](const Value&) { return std::optional<Value>(gate->get()); }));
  cell->restart(integer(10));
  gate->set(integer(999));
  REQUIRE(cell->send([](const Value& value) {
    return std::optional<Value>(integer(std::stoull(value.integer.to_decimal_string()) + 1));
  }));
  Cell::await_all_idle();
  CHECK(cell->get()->integer.to_decimal_string() == "11");
  CHECK(cell->status() == Cell::Status::Ready);
}

TEST_CASE("R25 shared Cell FIFO/failure case runs through the C++ engine fixture") {
  auto result = try_run(
      "state = ref(10)\n"
      "c = cell_with_state(state)\n"
      "cell_send(c, (x) -> x + 2)\n"
      "cell_send(c, (x) -> x * 3)\n"
      "cell_send(c, (_) -> 1 / 0)\n"
      "cell_send(c, (x) -> x + 1000)\n"
      "_r25_await_idle()\n"
      "[ref_get(state), cell_failed?(c), cell_status(c), some?(cell_error(c))]",
      true);
  REQUIRE(result.has_value());
  CHECK(result->stdout_text == "[36, true, \"failed\", true]\n");
}

TEST_CASE("R25 nested Cell sends commit only with the enclosing update") {
  auto result = try_run(
      "target = cell(10)\n"
      "committed = cell(0)\n"
      "discarded = cell(0)\n"
      "cell_send(committed, (x) -> {\n"
      "  cell_send(target, (n) -> n + 5)\n"
      "  x + 1\n"
      "})\n"
      "cell_send(discarded, (x) -> {\n"
      "  cell_send(target, (n) -> n + 100)\n"
      "  1 / 0\n"
      "})\n"
      "_r25_await_idle()\n"
      "[cell_get(committed), cell_get(target), cell_failed?(discarded)]",
      true);
  REQUIRE(result.has_value());
  CHECK(result->stdout_text == "[1, 15, true]\n");
}
