# Task: Pattern Matching (Dependent on Tuple Support)

## Goal

Implement structural pattern matching (`match` expression/statement) in
TinyPC (TPC), leveraging and building directly upon the tuple support
defined in `tasks/support_tuples.md` for destructuring multi-value
expressions, tuples, structs, and tagged unions.

## Syntax & Semantics

1. **`match` Expression / Statement:**
   -  Syntax:
     ```c
     match (expr) {
         case pattern1: body1;
         case pattern2(let x, let y): body2;
         default: body3;
     }
     ```
   -  Semantics: Evaluates `expr` once and matches it sequentially against
     patterns, binding destructuring variables in scope for matching branches.

2. **Pattern Kinds, Wildcard (`_`), and Ignore Operator (`@_`):**
   -  Literal patterns (`42`, `"hello"`)
   -  Wildcard / Catch-all patterns (`_`): Plain `_` can be used in C and D
     as a wildcard/ignore symbol when it does not shadow an existing
     identifier in scope.
   -  Binding / Identifier patterns (`let x`)
   -  **Ignore Operator (`@_`):** In compatibility mode, allows explicitly
     ignoring specific tuple elements or struct fields during
     destructuring (e.g., `case (x, @_ , y):`), avoiding unused variable
     warnings or strict positional binding requirements.
   -  Tuple destructuring patterns (`(a, b, c)`) leveraging runtime and
     compile-time tuples from `support_tuples.md`.
   -  Struct / Enum destructuring patterns (`Point { x, y }`).

## Dependencies

- [ ] **Prerequisite:** `tasks/support_tuples.md` (type and value tuples, tuple indexing and destructuring).

## Acceptance Criteria

- [ ] **P1:** Support `match` syntax with literal, wildcard, and identifier bindings.
- [ ] **P2:** Support tuple destructuring patterns in `match` (e.g., `case (x, y):`) utilizing tuple support.
- [ ] **P3:** Support the ignore operator `@_` in compatibility mode for ignoring unwanted tuple elements or struct fields.
- [ ] **P4:** Exhaustiveness checking and actionable compile-time diagnostics for unhandled patterns.
- [ ] **Verification:** `./configure && make -j$(nproc) -B && make -j$(nproc) test` passes with regression tests in `tests/tests2/`.
