// genia-cpp#50: Model B Slice A parser / portable Core IR lowering.
//
// Contract: genia-2026 GENIA_STATE.md "Model B declarations" and
// docs/architecture/core-ir-portability.md "Model B declaration carriers".
// The shared-case table is generated from genia-2026's 41 parse/IR
// `model-b-a-*` cases; the remaining tests pin host-local boundaries:
// syntax support never implies execution, legacy wire shapes are kept, and
// `IrPatSome.context` stays position dependent.
#include <string>

#include "../src/adapter.hpp"
#include "../third_party/catch2/catch.hpp"

namespace {

using json = nlohmann::json;

struct SharedCase {
  const char* name;
  const char* operation;
  const char* source;
  const char* expected;
};

const SharedCase kSharedCases[] = {
#include "model_b_slice_a_cases.inc"
};

json request(const std::string& operation, const std::string& source) {
  json payload{{"protocol_version", "1"},
               {"case_id", "model-b-a-host-local"},
               {"operation", operation},
               {"input", {{"source", source}}}};
  auto response = genia::adapter::handle_request(payload.dump());
  REQUIRE(response.has_value());
  return *response;
}

json lower_ok(const std::string& source) {
  auto response = request("lower", source);
  INFO(response.dump());
  REQUIRE(response["status"] == "ok");
  return response["result"]["ir"];
}

}  // namespace

TEST_CASE("model-b-a: every shared parse/IR case matches the pinned contract") {
  for (const auto& shared : kSharedCases) {
    INFO(shared.name);
    const json expected = json::parse(shared.expected);
    const json response = request(shared.operation, shared.source);
    INFO(response.dump());
    CHECK(response["status"] == "ok");
    if (response["status"] != "ok") continue;
    if (std::string(shared.operation) == "parse") {
      const json& result = response["result"];
      CHECK(result["kind"] == "error");
      CHECK(result["type"] == expected["type"]);
      const std::string message = result.value("message", "");
      CHECK(message.find(expected["message"].get<std::string>()) != std::string::npos);
    } else {
      CHECK(response["result"]["ir"] == expected);
    }
  }
}

TEST_CASE("model-b-a: syntax support never executes unsupported Model B runtime forms") {
  // immutable_bindings, closed_functions and default_open_functions are not
  // C++ capabilities; their programs must stay unsupported, never run as `=`.
  for (const char* source : {"x =: 1\nx", "f(x) =: x\nf(1)", "f([x]) = x\nf([1])",
                             "f(x) ? true = x\nf(1)", "f(..xs) = xs\nf(1, 2)",
                             "g = (P(x)) -> x\ng(1)", "g = (err(r)) -> r\ng(err(\"r\", 1))",
                             "@doc \"d\"\nx =: 1\nx", "f(_) = 1\nf(2)"}) {
    INFO(source);
    CHECK(request("eval", source)["status"] == "unsupported");
  }
}

TEST_CASE("model-b-a: =: lookalikes in strings, comments and maps keep their meaning") {
  CHECK(lower_ok("\"=:\"") == json::parse(R"([{"node":"IrExprStmt","expr":{"node":"IrLiteral","value":"=:"}}])"));
  CHECK(lower_ok("x = 1 # =: not an operator") ==
        json::parse(R"([{"node":"IrAssign","name":"x","expr":{"node":"IrLiteral","value":{"kind":"integer","digits":"1"}}}])"));
  CHECK(lower_ok("{a: 1}") ==
        json::parse(R"([{"node":"IrExprStmt","expr":{"node":"IrMap","items":[{"key":"a","value":{"node":"IrLiteral","value":{"kind":"integer","digits":"1"}}}]}}])"));
}

TEST_CASE("model-b-a: IrPatSome.context is serialized only inside pattern headers") {
  const json header = lower_ok("f(some(x, c)) = x");
  CHECK(header[0]["pattern"]["items"][0].contains("context"));
  const json lambda = lower_ok("g = (some(y, c)) -> y");
  CHECK_FALSE(lambda[0]["expr"]["pattern"]["items"][0].contains("context"));
  const json one_arg = lower_ok("f(some(x)) = x");
  CHECK_FALSE(one_arg[0]["pattern"]["items"][0].contains("context"));
}

TEST_CASE("model-b-a: IrPatErr serializes an absent context as null in every position") {
  const json lambda = lower_ok("g = (err(r)) -> r");
  CHECK(lambda[0]["expr"]["pattern"]["items"][0]["context"].is_null());
  const json grouped = lower_ok("f(x) = (err(r)) -> r | (_) -> 0");
  CHECK(grouped[0]["body"]["clauses"][0]["pattern"]["items"][0]["context"].is_null());
}

TEST_CASE("model-b-a: closed groups and annotations") {
  const json run = lower_ok("f(x) =: x\nf([y]) =: y");
  REQUIRE(run.size() == 1);
  CHECK(run[0]["node"] == "IrClosedFuncDef");
  CHECK(run[0]["declarations"][1]["node"] == "IrPatternFuncDef");
  // A closed group with a grouped body keeps the ordinary IrFuncDef child.
  CHECK(lower_ok("f(x) =: (y) -> y")[0]["declarations"][0]["body"]["node"] == "IrCase");
  // Nested =: binding inside a block lowers; executing it stays unsupported.
  const json nested = lower_ok("make(k) = { y =: k; y }");
  CHECK(nested[0]["body"]["exprs"][0]["node"] == "IrImmutableBind");
}

TEST_CASE("model-b-a: R20 and legacy behavior is unchanged") {
  auto gcd = request("eval", "open gcd(a, 0) = a\ngcd(a, b) = gcd(b, a % b)\ngcd(48, 18)");
  REQUIRE(gcd["status"] == "ok");
  CHECK(gcd["result"]["stdout"] == "6\n");
  auto grouped = request("eval", "f(x) = (0) -> \"zero\" | (_) -> \"other\"\nf(1)");
  REQUIRE(grouped["status"] == "ok");
  CHECK(grouped["result"]["stdout"] == "\"other\"\n");
  auto plain_block = request("eval", "inc(x) { x + 1 }\ninc(1)");
  REQUIRE(plain_block["status"] == "ok");
  CHECK(plain_block["result"]["stdout"] == "2\n");
}

// Regressions exposed once Slice A made more sources parse (shared cases
// spec/ir/option-patterns and spec/eval/model-b-c-open-lambda-factory-distinct).
TEST_CASE("model-b-a: bare none/nil are never identifier bind patterns") {
  // IrPatNone is not implemented by this host, so these stay unsupported
  // instead of lowering to a wrong IrPatBind("none").
  CHECK(request("lower", "f(opt) = (some(v)) -> v | (none) -> 0")["status"] == "unsupported");
  CHECK(request("lower", "g = (nil) -> 0")["status"] == "unsupported");
}

TEST_CASE("model-b-a: functions compare by runtime identity (R18)") {
  auto same = request("eval", "f = (x) -> x\ng = (x) -> x\n[f == f, f == g]");
  REQUIRE(same["status"] == "ok");
  CHECK(same["result"]["stdout"] == "[true, false]\n");
  auto factory = request("eval", "mk(k) = {\n  g = (x) -> x + k\n  g\n}\na = mk(1)\nb = mk(2)\n[a == a, a == b]");
  REQUIRE(factory["status"] == "ok");
  CHECK(factory["result"]["stdout"] == "[true, false]\n");
}
