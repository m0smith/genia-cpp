#include "../src/engine.hpp"
#include "../src/process.hpp"
#include "../third_party/catch2/catch.hpp"

using genia::engine::try_run;

TEST_CASE("R25 local Process provides causal Ref wakeup") {
  auto result = try_run(
      "r = ref()\n"
      "producer = spawn((value) -> ref_set(r, value))\n"
      "send(producer, 42)\n"
      "observed = ref_get(r)\n"
      "_r25_await_idle()\n"
      "[ref_is_set(r), observed]",
      true);
  REQUIRE(result.has_value());
  CHECK(result->stdout_text == "[true, 42]\n");
}

TEST_CASE("R25 local Process handles FIFO then fails permanently") {
  auto result = try_run(
      "log = ref([])\n"
      "handle(msg) =\n"
      "  \"boom\" -> 1 / 0 |\n"
      "  value -> ref_update(log, (xs) -> append(xs, [value]))\n"
      "p = spawn(handle)\n"
      "send(p, 1)\n"
      "send(p, 2)\n"
      "send(p, \"boom\")\n"
      "send(p, 3)\n"
      "_r25_await_idle()\n"
      "[ref_get(log), process_failed?(p), process_alive?(p), some?(process_error(p))]",
      true);
  REQUIRE(result.has_value());
  CHECK(result->stdout_text == "[[1, 2], true, false, true]\n");
}
