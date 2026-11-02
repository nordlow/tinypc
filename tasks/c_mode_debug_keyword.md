# Task: `debug` Keyword and `@debug` in C Mode

> **Status:** Proposed feature specification.

Enable the `debug` keyword (conditional compilation blocks like `debug { ... }`,
`debug(level) { ... }`, `debug(identifier) { ... }`) and `@debug` attribute/statement
form in **C mode** (`-lang=c`) as well as D mode (`-lang=d`).

## Goal

Provide built-in D-style conditional debugging blocks and attributes in standard C
mode, eliminating the need for repetitive `#ifdef DEBUG` preprocessor boilerplate.

## Semantics & Syntax

1. **`debug` Conditional Blocks:**
   - Supported in standard C mode:
     ```c
     debug {
         /* executed only when debugging is enabled */
         printf("debug info\n");
     }
     ```
2. **`@debug` Attribute / Statement:**
   - Functions or statements annotated with `@debug` compile conditionally based
     on debug flags.

## Implementation Details (`tccpp.c`, `tccgen.c`)

1. **Unconditional Keyword Recognition:**
   - Remove the `lang_d` gating check on `debug` and `@debug`, making them
     first-class syntax tokens available in all language modes.
2. **Conditional Compilation Logic:**
   - Reuse TinyPC's existing `debug(...)` block parser and conditional skipping
   - machinery (`check_d_conditional()`, `tccgen.c`) in C mode.

## Acceptance Criteria

- **P1:** Parse and evaluate `debug` blocks and `@debug` in C mode (`-lang=c`).
- **Verification:** `./configure && make -j$(nproc) -B && make -j$(nproc) test`
  passes with regression tests under `tests/tests2/`.
