// genia-cpp#50 review findings 1-4: adversarial malformed direct Core IR.
//
// Projection is the portable-IR boundary, so a malformed carrier must be
// refused (nullopt) rather than dereferenced. Each check builds IR directly,
// bypassing the parser, and expects refusal; a crash or partial output is a
// failure. Permitted shapes are checked too, so validation does not
// over-reject. Exit status 1 reports failed invariants.
#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "../src/ir_projection.hpp"

namespace {

using genia::core_ir::Kind;
using genia::core_ir::Node;
using genia::pattern::Pattern;
namespace pk = genia::pattern;

int g_failures = 0;

// Records a failed invariant with its description.
void expect(bool condition, const std::string& what) {
  if (!condition) {
    ++g_failures;
    std::cerr << "FAILED: " << what << "\n";
  }
}

// True when projection of `node` refuses it.
bool refused(const Node& node) { return !genia::ir_projection::project(node).has_value(); }

// A Pattern of `kind` with `count` wildcard children (and a name for Named).
Pattern carrier(pk::Kind kind, size_t count) {
  Pattern p;
  p.kind = kind;
  p.name = "P";
  for (size_t i = 0; i < count; ++i) p.items.push_back(Pattern::wildcard());
  return p;
}

// `f(<item>) = x` as a direct IrPatternFuncDef.
Node pattern_decl(Pattern item, std::string name = "f") {
  Node node;
  node.kind = Kind::PatternFuncDef;
  node.name = std::move(name);
  node.header = Pattern::tuple({std::move(item)});
  node.left = std::make_shared<Node>(Node::var("x"));
  return node;
}

// `g = (<item>) -> x` as a direct IrAssign of an IrLambda.
Node lambda_with(Pattern item) {
  return Node::assign("g", Node::lambda({Pattern::bind("y"), std::move(item)}, Node::var("x")));
}

// `f(x) = <body>` as a direct IrFuncDef.
Node func_decl(Node body, std::string name = "f") {
  return Node::func_def(std::move(name), {"x"}, {Pattern::bind("x")}, std::move(body));
}

// A closed group named `f` containing `children`.
Node closed(std::vector<Node> children) {
  Node group;
  group.kind = Kind::ClosedFuncDef;
  group.name = "f";
  group.items = std::move(children);
  return group;
}

// Finding 1: Err/Some/Named child counts are validated at every level.
void check_pattern_arity() {
  // One malformed carrier shape: a kind with a wrong child count.
  struct Bad {
    pk::Kind kind;
    size_t count;
  };
  // Err and Some carry exactly [value, context]; Named carries exactly [inner].
  for (const Bad bad : {Bad{pk::Kind::Err, 0}, Bad{pk::Kind::Err, 1}, Bad{pk::Kind::Err, 3},
                        Bad{pk::Kind::Some, 0}, Bad{pk::Kind::Some, 1}, Bad{pk::Kind::Some, 3},
                        Bad{pk::Kind::Named, 0}, Bad{pk::Kind::Named, 2}}) {
    const std::string what = "kind " + std::to_string(static_cast<int>(bad.kind)) + " with " +
                             std::to_string(bad.count) + " children";
    expect(refused(pattern_decl(carrier(bad.kind, bad.count))), "header " + what);
    expect(refused(lambda_with(carrier(bad.kind, bad.count))), "lambda " + what);
    expect(refused(pattern_decl(Pattern::list({carrier(bad.kind, bad.count)}))),
           "nested list " + what);
    Pattern outer = Pattern::named("Q", carrier(bad.kind, bad.count));
    expect(refused(pattern_decl(outer)), "inside Named " + what);
  }
  // Leaf carriers never carry children.
  Pattern bind_with_child = Pattern::bind("x");
  bind_with_child.items.push_back(Pattern::wildcard());
  expect(refused(pattern_decl(bind_with_child)), "Bind with a child");
  // Well-formed carriers are still accepted.
  expect(!refused(pattern_decl(Pattern::err(Pattern::bind("r"), Pattern::bind("c")))), "valid err");
  expect(!refused(pattern_decl(Pattern::err_without_context(Pattern::bind("r")))), "err(r)");
  expect(!refused(pattern_decl(Pattern::some(Pattern::bind("v"), Pattern::bind("c")))),
         "valid some");
  expect(!refused(pattern_decl(Pattern::named("P", Pattern::bind("x")))), "valid named");
}

// Finding 2: closed-group FuncDef children are fully validated.
void check_closed_children() {
  Node no_body = func_decl(Node::var("x"));
  no_body.left = nullptr;
  expect(refused(closed({no_body})), "closed child without a body");
  expect(refused(no_body), "top-level FuncDef without a body");

  Node grouped =
      Node::func_def_case("f", {"x"}, {Pattern::tuple({Pattern::wildcard()})}, {Node::var("x")});
  expect(!refused(closed({grouped})), "valid grouped closed child");
  Node fewer_results = grouped;
  fewer_results.case_patterns.push_back(Pattern::tuple({Pattern::wildcard()}));
  expect(refused(closed({fewer_results})), "more patterns than results");
  Node extra_results = grouped;
  extra_results.case_results.push_back(Node::var("y"));
  expect(refused(closed({extra_results})), "more results than patterns");
  Node short_guards = fewer_results;
  short_guards.case_results.push_back(Node::var("y"));
  short_guards.case_guards.push_back(nullptr);  // one guard slot for two clauses
  expect(refused(closed({short_guards})), "guard array shorter than clauses");
  Node extra_guards = grouped;
  extra_guards.case_guards = {nullptr, nullptr};
  expect(refused(closed({extra_guards})), "guard array longer than clauses");
  expect(refused(closed({grouped, no_body})), "one malformed child rejects the group");

  Node pattern_child = pattern_decl(Pattern::bind("x"));
  pattern_child.left = nullptr;
  expect(refused(closed({pattern_child})), "pattern child without a body");
}

// Finding 3: immutable bindings need an initializer.
void check_immutable_initializer() {
  Node bind;
  bind.kind = Kind::ImmutableBind;
  bind.name = "x";
  expect(refused(bind), "ImmutableBind without initializer");
  Node bad_init = bind;
  bad_init.left = std::make_shared<Node>(func_decl(Node::var("x")));
  bad_init.left->left = nullptr;
  expect(refused(bad_init), "ImmutableBind with malformed initializer");
  Node good = bind;
  good.left = std::make_shared<Node>(Node::var("y"));
  expect(!refused(good), "valid ImmutableBind");
}

// Finding 4: closed children may not contain nested declarations.
void check_nested_declarations() {
  Node inner = func_decl(Node::var("x"), "g");
  expect(refused(closed({func_decl(Node::block({inner, Node::var("g")}))})),
         "FuncDef nested in a closed child's block");
  Node inner_closed = closed({func_decl(Node::var("x"))});
  expect(refused(closed({func_decl(Node::block({inner_closed}))})),
         "closed group nested in a closed child's block");
  Node inner_pattern = pattern_decl(Pattern::bind("z"), "h");
  expect(refused(closed({func_decl(Node::block({inner_pattern}))})),
         "PatternFuncDef nested in a closed child's block");
  Node grouped = Node::func_def_case("f", {"x"}, {Pattern::tuple({Pattern::wildcard()})},
                                     {Node::block({inner, Node::var("g")})});
  expect(refused(closed({grouped})), "FuncDef nested in a grouped-arm result");
  Node pattern_body = pattern_decl(Pattern::bind("x"));
  pattern_body.left = std::make_shared<Node>(Node::block({inner}));
  expect(refused(closed({pattern_body})), "FuncDef nested in a pattern child's body");
  expect(refused(closed({func_decl(Node::lambda({Pattern::bind("y")}, Node::block({inner})))})),
         "FuncDef nested in a lambda body");

  // Permitted: local immutable bindings and ordinary nested expressions.
  Node local;
  local.kind = Kind::ImmutableBind;
  local.name = "y";
  local.left = std::make_shared<Node>(Node::var("x"));
  expect(!refused(closed({func_decl(Node::block({local, Node::var("y")}))})),
         "local immutable binding in a closed child");
  expect(!refused(closed({func_decl(Node::lambda({Pattern::bind("y")}, Node::var("y")))})),
         "lambda in a closed child");
  expect(!refused(closed({func_decl(Node::list({Node::call("h", {Node::var("x")})}))})),
         "ordinary call/list in a closed child");
}

// Runs `check` in a child process so a crash on malformed IR (the unsafe
// behaviour these checks guard against) is reported per check instead of
// ending the run. Returns true when the check passed.
bool run_isolated(const char* name, void (*check)()) {
  std::cout.flush();
  std::cerr.flush();
  const pid_t pid = fork();
  if (pid == 0) {
    check();
    std::_Exit(g_failures == 0 ? 0 : 1);
  }
  int status = 0;
  waitpid(pid, &status, 0);
  if (WIFEXITED(status) && WEXITSTATUS(status) == 0) return true;
  if (WIFSIGNALED(status)) {
    std::cerr << "FAILED: " << name << " crashed with signal " << WTERMSIG(status) << "\n";
  } else {
    std::cerr << "FAILED: " << name << "\n";
  }
  return false;
}

}  // namespace

// Runs every check in isolation; exit status 1 when any check failed.
int main() {
  int failed = 0;
  failed += run_isolated("finding_1_pattern_arity", check_pattern_arity) ? 0 : 1;
  failed += run_isolated("finding_2_closed_children", check_closed_children) ? 0 : 1;
  failed += run_isolated("finding_3_immutable_initializer", check_immutable_initializer) ? 0 : 1;
  failed += run_isolated("finding_4_nested_declarations", check_nested_declarations) ? 0 : 1;
  if (failed != 0) {
    std::cerr << failed << " malformed-IR check(s) failed\n";
    return 1;
  }
  std::cout << "Model B malformed-IR checks passed\n";
  return 0;
}
