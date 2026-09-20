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

TEST_CASE("parse: unsupported syntax (parens, strings) is std::nullopt, never a guess") {
  CHECK_FALSE(try_parse("(1 + 2)").has_value());
  CHECK_FALSE(try_parse("\"hello\"").has_value());
  CHECK_FALSE(try_parse("[1, 2]").has_value());
  CHECK_FALSE(try_parse("-5").has_value());  // unary minus is out of this slice's grammar
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
