"""Regenerate tests/model_b_slice_a_cases.inc from genia-2026's shared cases.

Development-only (never part of the build). Reads the parse/IR
``spec/*/model-b-a-*.yaml`` cases from a genia-2026 checkout at the pinned
contract revision and prints a C++ ``kSharedCases`` table to stdout:

    python3 -I tests/tools/gen_model_b_slice_a_cases.py <genia-2026> > tests/model_b_slice_a_cases.inc
"""
import glob
import json
import sys

import yaml


def lit(text):
    """Return ``text`` as a C++ raw string literal."""
    return 'R"GENIA(' + text + ')GENIA"'


def main(root):
    """Print the generated table for the checkout at ``root``."""
    rows = []
    for path in sorted(glob.glob(root + "/spec/*/model-b-a-*.yaml")):
        with open(path, encoding="utf-8") as handle:
            case = yaml.safe_load(handle)
        category = case["category"]
        if category == "eval":
            continue
        source = case["input"]["source"]
        if category == "parse":
            operation, expected = "parse", case["expected"]["parse"]
        else:
            operation, expected = "lower", case["expected"]["ir"]
        rows.append((case["name"], operation, source, json.dumps(expected, sort_keys=True)))
    print("// Generated from genia-2026 spec/{parse,ir}/model-b-a-*.yaml (%d cases)." % len(rows))
    print("// Do not edit by hand; regenerate when the pinned contract revision changes.")
    print("const SharedCase kSharedCases[] = {")
    for name, operation, source, expected in rows:
        print("    {%s, %s, %s, %s}," % (json.dumps(name), json.dumps(operation), lit(source), lit(expected)))
    print("};")


if __name__ == "__main__":
    main(sys.argv[1])
