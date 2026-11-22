# RED at a4673b7 + these tests (review findings 1-5)

## genia-model-b-malformed-ir-check
```
FAILED: finding_1_pattern_arity crashed with signal 11
FAILED: finding_2_closed_children crashed with signal 11
FAILED: finding_3_immutable_initializer crashed with signal 11
FAILED: FuncDef nested in a closed child's block
FAILED: closed group nested in a closed child's block
FAILED: PatternFuncDef nested in a closed child's block
FAILED: FuncDef nested in a grouped-arm result
FAILED: FuncDef nested in a pattern child's body
FAILED: FuncDef nested in a lambda body
FAILED: finding_4_nested_declarations
4 malformed-IR check(s) failed
rc=1
```
## genia-model-b-slice-a-check
```
FAILED: boolean_patterns: [{"body":{"node":"IrLiteral","value":{"digits":"1","kind":"integer"}},"guard":{"node":"IrLiteral","value":true},"name":"f","node":"IrPatternFuncDef","pattern":{"items":[{"name":"true","node":"IrPatBind"}],"node":"IrPatTuple"}}]
FAILED: boolean_patterns: [{"body":{"node":"IrLiteral","value":{"digits":"1","kind":"integer"}},"name":"f","node":"IrFuncDef","params":["false"]}]
FAILED: boolean_patterns: exception: [json.exception.type_error.305] cannot use operator[] with a string argument with null
3 Model B Slice A invariant(s) failed
rc=1
```

## Finding 1 follow-up: legacy Some context (RED at 96fa669)

```
FAILED: malformed context in a legacy lambda Some
FAILED: malformed context in a legacy grouped-arm Some
FAILED: malformed context in a Some nested in a list
FAILED: finding_1_some_context
1 malformed-IR check(s) failed
rc=1
```
