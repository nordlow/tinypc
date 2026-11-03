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
   -  Arguments passed to `macro` functions are type-checked against parameter
     declarations at the call site before expansion.
2. **Hygienic / Safe Expansion:**
   -  Prevents double-evaluation side effects (e.g., `MAX(a++, b++)` evaluates
     arguments safely or warns/errors).
3. **Type Parameters:**
   -  Ability to accept types as arguments (since templates are out of scope).

## Implementation Details

1. **Symbol Representation (`tcc.h`):**
   - [ ] Introduce a `SYM_MACRO` symbol flag or add `is_macro:1` to `SymAttr`
     to distinguish type-aware macro functions from ordinary functions and
     preprocessor macros.
   - [ ] Store the recorded body tokens (`TokenString *body_tokens`) directly in
     the `Sym` structure, similar to `InlineFunc` (`tcc.h:798`).

2. **Parser & Declaration (`tccgen.c`):**
   - [ ] Recognize `macro` as a function modifier specifier in `-lang=d` mode.
   - [ ] Parse formal parameters with explicit types.
   - [ ] Save the function body using `skip_or_save_block()` without emitting
     standalone object code for the macro definition itself.

3. **Call-Site Expansion & Type Checking (`tccgen.c`):**
   - [ ] Intercept calls to `SYM_MACRO` symbols during expression parsing in
     `unary()`.
   - [ ] Evaluate and type-check each argument expression against the formal
     parameter type.
   - [ ] Bind argument expressions (or temporaries to prevent double evaluation)
     and replay the saved body tokens into the parser stream using
     `begin_macro()` within the caller's expression scope.

4. **Code Generation:**
   - [ ] Because tokens are replayed directly into the parser at each call site,
     the backend compiles them inline within the caller's stack frame,
     achieving zero-cost abstraction with full type safety.

## Acceptance Criteria

- [ ] **P1:** Define parser and symbol representation for `macro` definitions and
  calls.
- [ ] **P2:** Implement type-checking for macro parameters and argument
  expressions, along with token-replay inline expansion.
- [ ] **Verification:** `./configure && make -j$(nproc) -B && make -j$(nproc) test`
  passes with new regression tests under `tests/tests2/`.
