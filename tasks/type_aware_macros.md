# Task: Type-Aware Macros a la C3 (`macro` keyword)

> **Status:** Proposed future feature specification.

Investigate and design C3-style type-aware macros (`macro` keyword) for TinyPC
(TPC). Type-aware macros combine the zero-cost inline expansion of macros with
type safety, proper scoping, and prevention of double-evaluation bugs common in
preprocessor macros.

## Goal

Support a `macro` keyword in `-lang=d` mode (or universally) to define
type-checked, expression-safe inline macro functions.

## Semantics & Design

1. **Type-Checking:**
   - Arguments passed to `macro` functions are type-checked against parameter
     declarations at the call site before expansion.
2. **Hygienic / Safe Expansion:**
   - Prevents double-evaluation side effects (e.g., `MAX(a++, b++)` evaluates
     arguments safely or warns/errors).
3. **Type Parameters:**
   - Ability to accept types as arguments (since templates are out of scope).

## Acceptance Criteria

- **P1:** Define parser and symbol representation for `macro` definitions and
  calls.
- **P2:** Implement type-checking for macro parameters and argument
  expressions.
- **Verification:** `./configure && make -j$(nproc) -B && make -j$(nproc) test`
  passes with regression tests under `tests/tests2/`.
