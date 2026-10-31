# Task: Type-Inferred Declaration Operator (`SYM := EXPR;`)

## Goal

Add support for the `SYM := EXPR;` assignment/declaration operator in
TinyPC (TPC) as a direct shorthand alias for `auto SYM = EXPR;` (type
inference declaration), inspired by Go, Zig, and Julia syntax.

## Syntax & Semantics

1. **Syntax:**
   - `x := 42;` $\to$ equivalent to `auto x = 42;` (`int x = 42;`).
   - `result := calculate();` $\to$ equivalent to `auto result = calculate();`.

2. **Semantics:**
   - Evaluates `EXPR` and infers the type of `SYM` from the expression type.
   - Declares `SYM` in the current lexical scope with inferred type and initializes it with the evaluated value of `EXPR`.
   - Disallows re-declaration with `:=` if `SYM` already exists in scope (unlike `=` assignment).

## Implementation Plan

1. **Lexer / Tokenizer (`tcctok.h`):**
   - Register `TOK_COLON_ASSIGN` (`:=`).
2. **Parser (`tccgen.c`):**
   - Recognize `IDENT :=` in declaration contexts (similar to `auto SYM =`).
   - Desugar `SYM := EXPR;` into `auto SYM = EXPR;` during parsing.

## Acceptance Criteria

- **P1:** Support `x := expr;` at block scope and global scope as an alias for `auto x = expr;`.
- **P2:** Correct type inference and scope binding for all primitive, pointer, struct, and slice types.
- **P3:** Actionable compile-time errors on re-declaration or invalid contexts.
- **Verification:** `./configure && make -j$(nproc) -B && make -j$(nproc) test` passes with regression tests under `tests/tests2/`.
