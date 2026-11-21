# Development-only generator (not part of the build): regenerates
# tests/model_b_slice_a_cases.inc from a genia-2026 checkout at the pinned
# contract revision. Usage: python3 -I tests/tools/gen_model_b_slice_a_cases.py <genia-2026> > tests/model_b_slice_a_cases.inc
import glob, json, sys, yaml, os
root = sys.argv[1]
out = []
for path in sorted(glob.glob(root + "/spec/*/model-b-a-*.yaml")):
    case = yaml.safe_load(open(path))
    cat = case["category"]
    if cat == "eval":
        continue
    src = case["input"]["source"]
    if cat == "parse":
        exp = case["expected"]["parse"]
        out.append((case["name"], "parse", src, json.dumps(exp, sort_keys=True)))
    else:
        out.append((case["name"], "lower", src, json.dumps(case["expected"]["ir"], sort_keys=True)))
def lit(s):
    return "R\"GENIA(" + s + ")GENIA\""
print("// Generated from genia-2026 spec/{parse,ir}/model-b-a-*.yaml (41 cases).")
print("// Do not edit by hand; regenerate when the pinned contract revision changes.")
for name, op, src, exp in out:
    print("{%s, %s, %s, %s}," % (json.dumps(name), json.dumps(op), lit(src), lit(exp)))
