#!/usr/bin/env python3
"""
genia-cpp bootstrap protocol-participation placeholder.

This is NOT a Genia host and does not interpret Genia source in any way.
It exists only to prove that this repository can participate in the
genia-2026 E16-1 host-adapter protocol conversation (declare a pinned
contract revision, answer a capabilities request, respond deterministically
to every operation) before any real C++ interpreter exists here.

Deliberately self-contained: it has no dependency on genia-2026's Python
package at runtime, matching the repository-boundary rule that this
repository owns its own toolchain. It reimplements only the tiny slice of
the E16-1 JSON envelope needed to respond -- see genia-2026's
tools/spec_runner/protocol.py for the authoritative protocol definition
this must stay compatible with.

Delete this file once R20 lands a real C++ adapter.

Run as: python3 bootstrap/protocol_adapter_stub.py
"""
from __future__ import annotations

import json
import sys

PROTOCOL_VERSION = "1"

# Pinned genia-2026 contract revision this bootstrap was built against.
# Update deliberately (see README.md's "Pinned contract" table) as this
# repository advances -- never silently.
CONTRACT_REVISION = "b0cf6921d15cea3205e2674117dddea67d7248b8"

_UNSUPPORTED_REASON = (
    "genia-cpp is bootstrap-only (no C++ interpreter exists yet); "
    "see https://github.com/m0smith/genia-cpp AGENTS.md"
)


def _unsupported_response(case_id: str, operation: str, reason: str) -> dict:
    return {
        "protocol_version": PROTOCOL_VERSION,
        "case_id": case_id,
        "operation": operation,
        "status": "unsupported",
        "result": None,
        "unsupported_reason": reason,
    }


def _capabilities_response(case_id: str) -> dict:
    # No capability is implemented yet; this repository has no C++
    # interpreter. Every capability name here must come from genia-2026's
    # shared vocabulary (spec/manifest.json / docs/host-interop/
    # capabilities.md), not be invented locally.
    known_capabilities = (
        "parser",
        "ast_lowering",
        "core_ir_eval",
        "cli_file_mode",
        "cli_command_mode",
        "cli_pipe_mode",
        "repl",
        "prelude_autoload",
        "doc_help",
        "shared_spec_runner",
        "flow_phase_1",
    )
    return {
        "protocol_version": PROTOCOL_VERSION,
        "case_id": case_id,
        "operation": "capabilities",
        "status": "ok",
        "result": {
            "capabilities": {name: "unsupported" for name in known_capabilities},
            "operations": [],
            "contract_revision": CONTRACT_REVISION,
            "protocol_version": PROTOCOL_VERSION,
        },
        "unsupported_reason": None,
    }


def handle(request: dict) -> dict:
    case_id = request["case_id"]
    operation = request["operation"]
    if operation == "capabilities":
        return _capabilities_response(case_id)
    return _unsupported_response(case_id, operation, _UNSUPPORTED_REASON)


def main(argv: list[str] | None = None) -> int:
    raw = sys.stdin.buffer.read()
    request = json.loads(raw.decode("utf-8"))
    response = handle(request)
    sys.stdout.buffer.write((json.dumps(response, sort_keys=True) + "\n").encode("utf-8"))
    sys.stdout.buffer.flush()
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
