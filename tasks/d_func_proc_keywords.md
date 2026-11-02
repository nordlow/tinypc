# Task: `func` (Pure by Default) and `proc` Keywords in D Mode

> **Status:** Proposed feature specification.

Implement D-style `func` and `proc` function declaration keywords in `-lang=d`
mode (inspired by languages like Mojo, Vale, and modern D dialects):
- **`func`:** Defines a pure function. `@pure` is inferred automatically, and
  side effects (mutating globals, calling impure functions) are rejected at
  compile time.
- **`proc`:** Defines a procedure. Permits side effects and state mutation
  normally (`@system` / impure by default).

## Goal

Provide concise, intent-revealing function declaration keywords in `-lang=d`
mode that enforce functional purity (`func`) or allow side effects (`proc`)
without requiring explicit `@pure` annotations.

## Semantics & Implementation Details

1. **Lexer & Keywords (`tcctok.h`, `tccpp.c`):**
   - Register `func` and `proc` keywords when `tcc_state->lang_d` is active.
2. **Parser & Attribute Inference (`tccgen.c`):**
   - When parsing a function defined with `func`, automatically set the
     `@pure` attribute flag on the function symbol.
   - When parsing a function defined with `proc`, leave purity unconstrained
     (or treat as `@system`/impure).
3. **Purity Enforcement:**
   - Reuse TinyPC's existing `@pure` purity analysis engine (`tasks/pure_attribute_semantics.md`)
     to validate that `func` bodies perform no illegal state mutations or
     calls to impure routines.

## Acceptance Criteria

- **P1:** Register `func` and `proc` tokens and parse them as function
  specifiers in `-lang=d` mode.
- **P2:** Automatically infer and enforce `@pure` semantics for `func`
  declarations, while permitting side effects for `proc`.
- **Verification:** `./configure && make -j$(nproc) -B && make -j$(nproc) test`
  passes with new regression tests under `tests/tests2/`.
