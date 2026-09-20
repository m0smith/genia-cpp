// Projects this slice's Core IR into the portable wire shape
// hosts/python/ir_normalize.py's `_normalize_ir_node` produces (the
// `lower` operation's `result.ir`). No `ir`/`lower`-category bootstrap
// case is pinned for E24-2 yet, but this is built honestly against the
// documented contract now rather than deferred, per AGENTS.md's
// "no parser-to-runtime shortcuts" rule -- the `lower` operation is
// real, not a stub, even before evidence exercises it.
#pragma once

#include <vector>

#include "../third_party/nlohmann_json/json.hpp"
#include "core_ir.hpp"

namespace genia::ir_projection {

using json = nlohmann::json;

inline json project(const core_ir::Node& node) {
  switch (node.kind) {
    case core_ir::Kind::Literal:
      return json{{"node", "IrLiteral"},
                  {"value", json{{"kind", "integer"}, {"digits", node.integer_digits}}}};
    case core_ir::Kind::Var:
      return json{{"node", "IrVar"}, {"name", node.name}};
    case core_ir::Kind::Binary:
      return json{{"node", "IrBinary"},
                  {"left", project(*node.left)},
                  {"op", core_ir::op_token_name(node.op)},
                  {"right", project(*node.right)}};
    case core_ir::Kind::ExprStmt:
      return json{{"node", "IrExprStmt"}, {"expr", project(*node.left)}};
  }
  return json();
}

// hosts/python/ir_normalize.py's normalize_portable_ir always returns a
// JSON array of normalized nodes, one per top-level program statement --
// unlike the `parse`-category projection, it never unwraps a
// single-statement program to a bare node.
inline json project_program(const std::vector<core_ir::Node>& program) {
  json array = json::array();
  for (const auto& node : program) {
    array.push_back(project(node));
  }
  return array;
}

}  // namespace genia::ir_projection
