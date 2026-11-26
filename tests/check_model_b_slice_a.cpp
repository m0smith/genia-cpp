// genia-cpp#50: Model B Slice A parser / portable Core IR lowering checks.
//
// Contract: genia-2026 GENIA_STATE.md "Model B declarations" and
// docs/architecture/core-ir-portability.md "Model B declaration carriers".
// The shared-case table is generated from genia-2026's 41 parse/IR
// `model-b-a-*` cases (tests/tools/gen_model_b_slice_a_cases.py). The other
// checks pin host-local boundaries: syntax support never implies execution,
// legacy wire shapes are kept, and `IrPatSome.context` stays position
// dependent. Exit status 1 reports failed invariants; no timing or network.
#include <iostream>
#include <stdexcept>
#include <string>

#include "../src/adapter.hpp"

namespace {

using json = nlohmann::json;

// One generated shared case: operation, source and expected wire result.
struct SharedCase {
  const char* name;
  const char* operation;
  const char* source;
  const char* expected;
};

#include "model_b_slice_a_cases.inc"

int g_failures = 0;
std::string g_current_check;

// Records a false invariant with its context without ending the check.
void expect(bool condition, const std::string& context = "") {
  if (!condition) {
    ++g_failures;
    std::cerr << "FAILED: " << g_current_check << (context.empty() ? "" : ": " + context) << "\n";
  }
}

// Stops a check whose prerequisite failed, so later accesses stay safe.
void require(bool condition, const std::string& context = "") {
  expect(condition, context);
  if (!condition) throw std::runtime_error("prerequisite failed");
}

// Sends one E16-1 request through the adapter and returns its response.
json request(const std::string& operation, const std::string& source) {
  json payload{{"protocol_version", "1"},
               {"case_id", "model-b-a-host-local"},
               {"operation", operation},
               {"input", {{"source", source}}}};
  auto response = genia::adapter::handle_request(payload.dump());
  require(response.has_value(), "no adapter response");
  return *response;
}

// Lowers `source`, requiring an ok response; returns the Core IR array.
json lower_ok(const std::string& source) {
  auto response = request("lower", source);
  require(response["status"] == "ok", source + " -> " + response.dump());
  return response["result"]["ir"];
}

// Every shared parse/IR case matches the pinned contract wire result.
void check_shared_cases() {
  for (const auto& shared : kSharedCases) {
    const json expected = json::parse(shared.expected);
    const json response = request(shared.operation, shared.source);
    const std::string context = std::string(shared.name) + " -> " + response.dump();
    expect(response["status"] == "ok", context);
    if (response["status"] != "ok") continue;
    if (std::string(shared.operation) == "parse") {
      const json& result = response["result"];
      expect(result["kind"] == "error" && result["type"] == expected["type"], context);
      const std::string message = result.value("message", "");
      expect(message.find(expected["message"].get<std::string>()) != std::string::npos, context);
    } else {
      expect(response["result"]["ir"] == expected, context);
    }
  }
}

// Syntax support never executes unsupported Model B runtime forms:
// immutable_bindings, closed_functions and default_open_functions are not
// C++ capabilities, so these programs stay unsupported, never run as `=`.
void check_no_execution() {
  for (const char* source :
       {"x =: 1\nx", "f(x) =: x\nf(1)", "f([x]) = x\nf([1])", "f(x) ? true = x\nf(1)",
        "f(..xs) = xs\nf(1, 2)", "g = (P(x)) -> x\ng(1)", "g = (err(r)) -> r\ng(err(\"r\", 1))",
        "@doc \"d\"\nx =: 1\nx", "f(_) = 1\nf(2)"}) {
    expect(request("eval", source)["status"] == "unsupported", source);
  }
}

// `=:` lookalikes in strings, comments and maps keep their meaning.
void check_lookalikes() {
  expect(lower_ok("\"=:\"") ==
         json::parse(R"([{"node":"IrExprStmt","expr":{"node":"IrLiteral","value":"=:"}}])"));
  expect(
      lower_ok("x = 1 # =: not an operator") ==
      json::parse(
          R"([{"node":"IrAssign","name":"x","expr":{"node":"IrLiteral","value":{"kind":"integer","digits":"1"}}}])"));
  expect(
      lower_ok("{a: 1}") ==
      json::parse(
          R"([{"node":"IrExprStmt","expr":{"node":"IrMap","items":[{"key":"a","value":{"node":"IrLiteral","value":{"kind":"integer","digits":"1"}}}]}}])"));
}

// IrPatSome.context is serialized only inside pattern headers.
void check_some_context_asymmetry() {
  expect(lower_ok("f(some(x, c)) = x")[0]["pattern"]["items"][0].contains("context"));
  expect(!lower_ok("g = (some(y, c)) -> y")[0]["expr"]["pattern"]["items"][0].contains("context"));
  expect(!lower_ok("f(some(x)) = x")[0]["pattern"]["items"][0].contains("context"));
}

// IrPatErr serializes an absent context as null in every position.
void check_err_null_context() {
  expect(lower_ok("g = (err(r)) -> r")[0]["expr"]["pattern"]["items"][0]["context"].is_null());
  const json grouped = lower_ok("f(x) = (err(r)) -> r | (_) -> 0");
  expect(grouped[0]["body"]["clauses"][0]["pattern"]["items"][0]["context"].is_null());
}

// Closed groups merge consecutive clauses; nested `=:` bindings lower.
void check_closed_groups() {
  const json run = lower_ok("f(x) =: x\nf([y]) =: y");
  require(run.size() == 1, run.dump());
  expect(run[0]["node"] == "IrClosedFuncDef");
  expect(run[0]["declarations"][1]["node"] == "IrPatternFuncDef");
  expect(lower_ok("f(x) =: (y) -> y")[0]["declarations"][0]["body"]["node"] == "IrCase");
  const json nested = lower_ok("make(k) = { y =: k; y }");
  expect(nested[0]["body"]["exprs"][0]["node"] == "IrImmutableBind");
}

// Malformed direct closed groups and pattern headers are refused, not serialized.
void check_malformed_carriers() {
  using genia::core_ir::Kind;
  using genia::core_ir::Node;
  Node empty;
  empty.kind = Kind::ClosedFuncDef;
  empty.name = "f";
  expect(!genia::ir_projection::project(empty).has_value(), "empty closed group");
  Node child = Node::func_def("g", {"x"}, {genia::pattern::Pattern::bind("x")}, Node::var("x"));
  Node mismatched = empty;
  mismatched.items.push_back(child);
  expect(!genia::ir_projection::project(mismatched).has_value(), "child name mismatch");
  Node nested = empty;
  nested.items.push_back(empty);
  expect(!genia::ir_projection::project(nested).has_value(), "nested closed group");
  Node header;
  header.kind = Kind::PatternFuncDef;
  header.name = "f";
  header.header = genia::pattern::Pattern::bind("x");
  header.left = std::make_shared<Node>(Node::var("x"));
  expect(!genia::ir_projection::project(header).has_value(), "non-tuple header");
  header.header = genia::pattern::Pattern::tuple({genia::pattern::Pattern::bind("x")});
  header.is_case_body = true;
  expect(!genia::ir_projection::project(header).has_value(), "grouped pattern body");
}

// R20 and legacy evaluation behavior is unchanged.
void check_r20_and_legacy() {
  auto gcd = request("eval", "open gcd(a, 0) = a\ngcd(a, b) = gcd(b, a % b)\ngcd(48, 18)");
  require(gcd["status"] == "ok", gcd.dump());
  expect(gcd["result"]["stdout"] == "6\n");
  auto grouped = request("eval", "f(x) = (0) -> \"zero\" | (_) -> \"other\"\nf(1)");
  require(grouped["status"] == "ok", grouped.dump());
  expect(grouped["result"]["stdout"] == "\"other\"\n");
  auto plain_block = request("eval", "inc(x) { x + 1 }\ninc(1)");
  require(plain_block["status"] == "ok", plain_block.dump());
  expect(plain_block["result"]["stdout"] == "2\n");
}

// Bare none/nil project as absence patterns, never identifier bindings.
void check_none_nil_patterns() {
  const auto clause = request("lower", "f(opt) = (some(v)) -> v | (none) -> 0");
  const auto lambda = request("lower", "g = (nil) -> 0");
  expect(clause["status"] == "ok" && clause.dump().find("IrPatNone") != std::string::npos);
  expect(lambda["status"] == "ok" && lambda.dump().find("IrPatNone") != std::string::npos);
}

// Functions compare by runtime identity (R18; exposed by
// spec/eval/model-b-c-open-lambda-factory-distinct).
void check_function_identity() {
  auto same = request("eval", "f = (x) -> x\ng = (x) -> x\n[f == f, f == g]");
  require(same["status"] == "ok", same.dump());
  expect(same["result"]["stdout"] == "[true, false]\n");
  auto factory = request(
      "eval", "mk(k) = {\n  g = (x) -> x + k\n  g\n}\na = mk(1)\nb = mk(2)\n[a == a, a == b]");
  require(factory["status"] == "ok", factory.dump());
  expect(factory["result"]["stdout"] == "[true, false]\n");
}

// Review finding 3 (source path): a missing `=:` initializer stays a parse
// SyntaxError and is never lowered.
void check_missing_initializer_source() {
  auto parsed = request("parse", "x =:");
  require(parsed["status"] == "ok", parsed.dump());
  expect(parsed["result"]["kind"] == "error" && parsed["result"]["type"] == "SyntaxError");
  expect(request("lower", "x =:")["status"] == "unsupported");
}

// Review finding 5: bare true/false are Boolean literal patterns in every
// position (wire `IrPatLiteral` with a JSON boolean), never identifier binds.
void check_boolean_patterns() {
  const json lit_true = json::parse(R"({"node":"IrPatLiteral","value":true})");
  const json lit_false = json::parse(R"({"node":"IrPatLiteral","value":false})");
  const json guarded = lower_ok("f(true) ? true = 1");
  expect(guarded[0]["node"] == "IrPatternFuncDef", guarded.dump());
  expect(guarded[0]["pattern"]["items"][0] == lit_true, guarded.dump());
  const json unguarded = lower_ok("f(false) = 1");
  expect(unguarded[0]["node"] == "IrPatternFuncDef", unguarded.dump());
  expect(unguarded[0]["pattern"]["items"][0] == lit_false, unguarded.dump());
  const json compound = lower_ok("f([true, {k: false}]) = 1");
  expect(compound[0]["pattern"]["items"][0]["items"][0] == lit_true, compound.dump());
  expect(compound[0]["pattern"]["items"][0]["items"][1]["items"][0]["value"] == lit_false,
         compound.dump());
  const json closed = lower_ok("f(true) =: 1");
  expect(closed[0]["declarations"][0]["node"] == "IrPatternFuncDef", closed.dump());
  const json lambda = lower_ok("g = (true) -> 1");
  expect(lambda[0]["expr"]["pattern"]["items"][0] == lit_true, lambda.dump());
  // Real identifiers keep identifier-binding semantics.
  const json ident = lower_ok("f(truth, falsey) = truth");
  expect(ident[0]["node"] == "IrFuncDef" && ident[0]["params"] == json({"truth", "falsey"}),
         ident.dump());
  // A Boolean literal arm matches by equality, not as a catch-all binding.
  auto arms = request("eval", "f(x) = (true) -> \"t\" | (_) -> \"o\"\n[f(true), f(false), f(1)]");
  require(arms["status"] == "ok", arms.dump());
  expect(arms["result"]["stdout"] == "[\"t\", \"o\", \"o\"]\n", arms.dump());
}

// Runs one named check, isolating a failed prerequisite to that check.
void run_check(const char* name, void (*check)()) {
  g_current_check = name;
  try {
    check();
  } catch (const std::runtime_error&) {
  } catch (const std::exception& error) {
    // A malformed response shape is a failure of this check, not a crash.
    expect(false, std::string("exception: ") + error.what());
  }
}

}  // namespace

// Runs every check; exit status 1 when any invariant failed.
int main() {
  run_check("shared_cases", check_shared_cases);
  run_check("no_execution", check_no_execution);
  run_check("lookalikes", check_lookalikes);
  run_check("some_context_asymmetry", check_some_context_asymmetry);
  run_check("err_null_context", check_err_null_context);
  run_check("closed_groups", check_closed_groups);
  run_check("malformed_carriers", check_malformed_carriers);
  run_check("r20_and_legacy", check_r20_and_legacy);
  run_check("none_nil_patterns", check_none_nil_patterns);
  run_check("function_identity", check_function_identity);
  run_check("missing_initializer_source", check_missing_initializer_source);
  run_check("boolean_patterns", check_boolean_patterns);
  if (g_failures != 0) {
    std::cerr << g_failures << " Model B Slice A invariant(s) failed\n";
    return 1;
  }
  std::cout << "Model B Slice A checks passed\n";
  return 0;
}
