# Task: `func` (Pure by Default) and `proc` Keywords in D Mode

> **Status:** Proposed feature specification.

Implement D-style `func` and `proc` function declaration keywords in `-lang=d`
mode (inspired by languages like Mojo, Vale, and modern D dialects):
- **`func`:** Defines a pure function. `@pure` is inferred automatically, and
  side effects (mutating globals, calling impure functions) are rejected at
  compile time. Both `func` and `proc` are **`@safe` by default**.
- **`proc`:** Defines a procedure. Permits side effects and state mutation
  normally (impure), but remains **`@safe` by default** (raw pointer arithmetic
  is forbidden unless `@system` or `@trusted` is explicitly specified).

## Goal

Provide concise, intent-revealing function declaration keywords in `-lang=d`
mode that enforce functional purity (`func`) or allow side effects (`proc`),
with all code operating under `@safe` safety checks by default.

## Semantics & Implementation Details

1. **Safety by Default:**
   - In `-lang=d` mode, all code (`func` and `proc` alike) is `@safe` by
     default. Unsafe low-level pointer arithmetic requires explicit `@system`
     or `@trusted` annotations or blocks.
2. **Lexer & Keywords (`tcctok.h`, `tccpp.c`):**
   - Register `func` and `proc` keywords when `tcc_state->lang_d` is active.
3. **Parser & Attribute Inference (`tccgen.c`):**
   - When parsing a function defined with `func`, automatically set both the
     `@safe` and `@pure` attribute flags on the function symbol.
   - When parsing a function defined with `proc`, set `@safe` by default
     while leaving purity unconstrained (impure).
4. **Enforcement:**
   - Reuse TinyPC's existing `@safe` memory check and `@pure` purity analysis
     engines (`tasks/universal_safe_memory_checks.md` and
     `tasks/pure_attribute_semantics.md`).

## Acceptance Criteria

- [x] **P1:** Register `func` and `proc` tokens and parse them as function
  specifiers in `-lang=d` mode.
- [x] **P2:** Automatically infer and enforce `@safe` by default for both `func`
  and `proc`, and `@pure` semantics for `func`.
- [x] **Verification:** `./configure && make -j$(nproc) -B && make -j$(nproc) test`
  passes with new regression tests under `tests/tests2/`.
