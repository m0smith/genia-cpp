// Projects this slice's Core IR into the portable wire shape
// hosts/python/ir_normalize.py's `_normalize_ir_node` produces (the
// `lower` operation's `result.ir`). No `ir`/`lower`-category bootstrap
// case is pinned for E24-2/E24-3 yet, but this is built honestly
// against the documented contract now rather than deferred, per
// AGENTS.md's "no parser-to-runtime shortcuts" rule -- the `lower`
// operation is real, not a stub, even before evidence exercises it.
#pragma once

#include <vector>

#include "../third_party/nlohmann_json/json.hpp"
#include "core_ir.hpp"

namespace genia::ir_projection {

using json = nlohmann::json;

inline json project(const core_ir::Node& node) {
  switch (node.kind) {
    case core_ir::Kind::Literal:
      switch (node.literal_kind) {
        case core_ir::LiteralKind::Integer:
          return json{{"node", "IrLiteral"},
                      {"value", json{{"kind", "integer"}, {"digits", node.integer_digits}}}};
        case core_ir::LiteralKind::String:
          return json{{"node", "IrLiteral"}, {"value", node.string_value}};
        case core_ir::LiteralKind::Bool:
          return json{{"node", "IrLiteral"}, {"value", node.bool_value}};
      }
      return json();
    case core_ir::Kind::Var:
      return json{{"node", "IrVar"}, {"name", node.name}};
    case core_ir::Kind::Binary:
      return json{{"node", "IrBinary"},
                  {"left", project(*node.left)},
                  {"op", core_ir::op_token_name(node.op)},
                  {"right", project(*node.right)}};
    case core_ir::Kind::ExprStmt:
      return json{{"node", "IrExprStmt"}, {"expr", project(*node.left)}};
    case core_ir::Kind::List: {
      json items = json::array();
      for (const auto& item : node.items) {
        items.push_back(project(item));
      }
      return json{{"node", "IrList"}, {"items", items}};
    }
    case core_ir::Kind::Assign:
      return json{{"node", "IrAssign"}, {"name", node.name}, {"expr", project(*node.left)}};
    case core_ir::Kind::Call: {
      json args = json::array();
      for (const auto& arg : node.items) {
        args.push_back(project(arg));
      }
      return json{
          {"node", "IrCall"}, {"fn", json{{"node", "IrVar"}, {"name", node.name}}}, {"args", args}};
    }
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
