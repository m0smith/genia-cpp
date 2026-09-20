// Unit tests for the E24-2 vertical slice's parse/lower/eval pipeline
// (src/engine.hpp and friends). These exercise the same behavior the
// pinned genia-2026 bootstrap-cases.json evidence requires, plus the
// deliberate "unsupported, never wrong" boundary this slice must hold
// for anything outside its grammar.
#include "../src/ast_projection.hpp"
#include "../src/engine.hpp"
#include "../src/ir_projection.hpp"
#include "../third_party/catch2/catch.hpp"

using genia::engine::try_lower;
using genia::engine::try_parse;
using genia::engine::try_run;

TEST_CASE("parse: a bare integer literal projects as {kind: Literal, value: N}") {
  auto ast = try_parse("42");
  REQUIRE(ast.has_value());
  CHECK((*ast)["kind"] == "Literal");
  CHECK((*ast)["value"] == 42);
}

TEST_CASE("parse: 1 2 3 is three independent top-level statements, not one") {
  auto ast = try_parse("1 2 3");
  REQUIRE(ast.has_value());
  REQUIRE(ast->is_array());
  REQUIRE(ast->size() == 3);
  CHECK((*ast)[0]["value"] == 1);
  CHECK((*ast)[1]["value"] == 2);
  CHECK((*ast)[2]["value"] == 3);
}

TEST_CASE("parse: 40 + 2 projects as a single Binary node") {
  auto ast = try_parse("40 + 2");
  REQUIRE(ast.has_value());
  CHECK((*ast)["kind"] == "Binary");
  CHECK((*ast)["op"] == "+");
  CHECK((*ast)["left"]["value"] == 40);
  CHECK((*ast)["right"]["value"] == 2);
}

TEST_CASE("parse: print 123 is a Var reference followed by a Literal") {
  auto ast = try_parse("print 123");
  REQUIRE(ast.has_value());
  REQUIRE(ast->is_array());
  REQUIRE(ast->size() == 2);
  CHECK((*ast)[0]["kind"] == "Var");
  CHECK((*ast)[0]["name"] == "print");
  CHECK((*ast)[1]["kind"] == "Literal");
  CHECK((*ast)[1]["value"] == 123);
}

TEST_CASE("parse: unsupported syntax (parens, lambdas) is std::nullopt, never a guess") {
  CHECK_FALSE(try_parse("(1 + 2)").has_value());
  CHECK_FALSE(try_parse("(x) -> x").has_value());
  CHECK_FALSE(try_parse("-5").has_value());  // unary minus is out of this slice's grammar
}

TEST_CASE("parse: string and list literals are supported as of E24-3") {
  auto string_ast = try_parse("\"hello\"");
  REQUIRE(string_ast.has_value());
  CHECK((*string_ast)["kind"] == "String");

  auto list_ast = try_parse("[1, 2]");
  REQUIRE(list_ast.has_value());
  CHECK((*list_ast)["kind"] == "List");
}

TEST_CASE("lower: a literal lowers to a tagged IrLiteral payload wrapped in IrExprStmt") {
  auto ir = try_lower("42");
  REQUIRE(ir.has_value());
  REQUIRE(ir->is_array());
  REQUIRE(ir->size() == 1);
  const auto& stmt = (*ir)[0];
  CHECK(stmt["node"] == "IrExprStmt");
  const auto& node = stmt["expr"];
  CHECK(node["node"] == "IrLiteral");
  CHECK(node["value"]["kind"] == "integer");
  CHECK(node["value"]["digits"] == "42");
}

TEST_CASE("lower: a binary expression lowers to IrBinary with token-name op") {
  auto ir = try_lower("40 + 2");
  REQUIRE(ir.has_value());
  REQUIRE(ir->size() == 1);
  const auto& node = (*ir)[0]["expr"];
  CHECK(node["node"] == "IrBinary");
  CHECK(node["op"] == "PLUS");
  CHECK(node["left"]["node"] == "IrLiteral");
  CHECK(node["right"]["node"] == "IrLiteral");
}

TEST_CASE("lower: top-level statements are wrapped in IrExprStmt, matching genia-2026 evidence") {
  // Regression for the r21-integer-literal-tagged-payload.yaml /
  // r21-slash-remains-ordinary-binary.yaml shared cases, which expect
  // every top-level statement wrapped, never a bare top-level node.
  auto ir = try_lower("10 / 2");
  REQUIRE(ir.has_value());
  REQUIRE(ir->size() == 1);
  CHECK((*ir)[0]["node"] == "IrExprStmt");
  CHECK((*ir)[0]["expr"]["node"] == "IrBinary");
  CHECK((*ir)[0]["expr"]["op"] == "SLASH");
}

TEST_CASE("parse: a digit run immediately followed by 'e' or '.' is unsupported, never split") {
  // Regression: "1e3"/"100e-2" are single Decimal-literal source
  // attempts in genia-2026's real grammar (R21), never an Integer
  // token immediately followed by an unrelated bare identifier.
  CHECK_FALSE(try_parse("1e3").has_value());
  CHECK_FALSE(try_parse("100e-2").has_value());
  CHECK_FALSE(try_parse("1e").has_value());
  CHECK_FALSE(try_parse("1.5").has_value());
  CHECK_FALSE(try_run("1e3").has_value());
}

TEST_CASE("parse: an identifier other than the one evidenced global name is unsupported") {
  // Regression: genia-2026 keywords like `none` lower to their own Core
  // IR node (IrOptionNone), never IrVar -- accepting arbitrary
  // identifiers as ordinary Var references would silently misparse
  // them. Only "print" is accepted (see global_env.hpp).
  CHECK_FALSE(try_parse("none").has_value());
  CHECK_FALSE(try_parse("some").has_value());
  CHECK_FALSE(try_parse("undefined_name_xyz").has_value());
  CHECK(try_parse("print").has_value());
}

TEST_CASE("run: arithmetic-basic.yaml -- 40 + 2 produces stdout 42\\n") {
  auto result = try_run("40 + 2");
  REQUIRE(result.has_value());
  CHECK(result->stdout_text == "42\n");
  CHECK(result->stderr_text.empty());
  CHECK(result->exit_code == 0);
}

TEST_CASE("run: integer-arithmetic-large-magnitude-no-overflow.yaml exact product") {
  auto result = try_run("99999999999999999999999999999999 * 99999999999999999999999999999999");
  REQUIRE(result.has_value());
  CHECK(result->stdout_text ==
        "9999999999999999999999999999999800000000000000000000000000000001\n");
}

TEST_CASE("run: print 123 -- the Var reference is inert, only the literal is displayed") {
  auto result = try_run("print 123");
  REQUIRE(result.has_value());
  CHECK(result->stdout_text == "123\n");
  CHECK(result->exit_code == 0);
}

TEST_CASE("run: an undefined name is unsupported, never a guessed diagnostic") {
  CHECK_FALSE(try_run("undefined_name_xyz").has_value());
}

TEST_CASE("run: non-evenly-dividing division is unsupported (would require Rational)") {
  CHECK_FALSE(try_run("1 / 2").has_value());
}

TEST_CASE("run: evenly-dividing division produces an exact Integer") {
  auto result = try_run("6 / 3");
  REQUIRE(result.has_value());
  CHECK(result->stdout_text == "2\n");
}

TEST_CASE("run: division by zero is unsupported, never a crash or a fabricated error") {
  CHECK_FALSE(try_run("1 / 0").has_value());
}

TEST_CASE("run: unary minus is unsupported at this slice") {
  CHECK_FALSE(try_run("-5").has_value());
}

TEST_CASE("run: an empty program is unsupported") { CHECK_FALSE(try_run("").has_value()); }

TEST_CASE("run: standard precedence -- multiplication binds tighter than addition") {
  auto result = try_run("2 + 3 * 4");
  REQUIRE(result.has_value());
  CHECK(result->stdout_text == "14\n");
}

// --- E24-3: lists, ordered maps, equality, file mode ------------------

TEST_CASE("run: list-literal-construction-and-equality.yaml") {
  auto result = try_run("[1, 2] == [1, 2]");
  REQUIRE(result.has_value());
  CHECK(result->stdout_text == "true\n");
}

TEST_CASE("run: map-items.yaml") {
  auto result = try_run(
      "m = map_put(map_put(map_new(), \"a\", 1), \"b\", 2)\n"
      "map_items(m)\n");
  REQUIRE(result.has_value());
  CHECK(result->stdout_text == "[[\"a\", 1], [\"b\", 2]]\n");
}

TEST_CASE("run: map-put-replace-existing-key-preserves-order-via-items.yaml") {
  auto result = try_run(
      "m = map_put(map_put(map_put(map_new(), \"a\", 1), \"b\", 2), \"c\", 3)\n"
      "map_items(map_put(m, \"a\", 99))\n");
  REQUIRE(result.has_value());
  CHECK(result->stdout_text == "[[\"a\", 99], [\"b\", 2], [\"c\", 3]]\n");
}

TEST_CASE("run: r18-structural-equality-bytes-and-lists.yaml") {
  auto result = try_run(
      "a = utf8_encode(\"hi\")\n"
      "b = utf8_encode(\"hi\")\n"
      "c = utf8_encode(\"no\")\n"
      "[a == b, a == c, [a] == [b], [1, 2] == [1, 2], [1, 2] == [1, 2, 3], [] == []]\n");
  REQUIRE(result.has_value());
  CHECK(result->stdout_text == "[true, false, true, true, false, true]\n");
}

TEST_CASE("run: r18-legal-key-kind-separation-integer-string.yaml") {
  auto result = try_run(
      "bools = map_put(map_put(map_new(), true, \"bool\"), 1, \"int\")\n"
      "kinds = map_put(map_put(map_new(), \"1\", \"string\"), 1, \"int\")\n"
      "[map_count(bools), map_get(bools, true), map_get(bools, 1), map_count(kinds), "
      "map_get(kinds, \"1\"), map_get(kinds, 1)]\n");
  REQUIRE(result.has_value());
  CHECK(result->stdout_text == "[2, \"bool\", \"int\", 2, \"string\", \"int\"]\n");
}

TEST_CASE("run: file_mode_basic.genia's source (print 42) still works via the shared pipeline") {
  auto result = try_run("print 42");
  REQUIRE(result.has_value());
  CHECK(result->stdout_text == "42\n");
}

TEST_CASE("run: assignment introduces a binding visible to later statements only") {
  auto result = try_run("x = 5\nx + 1\n");
  REQUIRE(result.has_value());
  CHECK(result->stdout_text == "6\n");
}

TEST_CASE("run: referencing an undefined name is unsupported even with assignment support") {
  CHECK_FALSE(try_run("y").has_value());
}

TEST_CASE("run: boolean literals render as true/false, distinct kind from Integer") {
  auto result = try_run("[true, false, true == 1, 1 == true]");
  REQUIRE(result.has_value());
  CHECK(result->stdout_text == "[true, false, false, false]\n");
}

TEST_CASE("run: map_get on a legal key with no matching entry is unsupported, not a crash") {
  CHECK_FALSE(try_run("map_get(map_new(), \"missing\")").has_value());
}

TEST_CASE("run: map_put/map_get round trip through a native ordered map") {
  auto result = try_run("map_get(map_put(map_new(), \"k\", 7), \"k\")");
  REQUIRE(result.has_value());
  CHECK(result->stdout_text == "7\n");
}

TEST_CASE("run: string escapes are unsupported, never guessed at") {
  CHECK_FALSE(try_run("\"a\\\"b\"").has_value());
}
