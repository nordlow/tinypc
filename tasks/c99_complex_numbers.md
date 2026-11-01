# Task: C99 Complex and Imaginary Numbers (`_Complex`)

> **Status:** Proposed feature specification.

Implement C99 complex and imaginary number support (`_Complex float`,
`_Complex double`, `_Complex long double`, and `_Imaginary`) in TinyPC (TPC),
enabling complex arithmetic, type representation, and standard library
integration via `<complex.h>`.

## Goal

Support C99 complex types (`float _Complex`, `double _Complex`,
`long double _Complex` and their keyword/macro aliases `float complex`, etc.)
in the parser, type system (`CType`), code generator, and runtime ABI.

## Implementation Details

1. **Lexer & Keyword Support (`tcctok.h`, `tccpp.c`):**
   - Register `_Complex` and `_Imaginary` keywords.
   - Support standard header mapping via `<complex.h>` and `complex` /
     `imaginary` macros.
2. **Type System (`tcc.h`, `tccgen.c`):**
   - Extend `VT_BTYPE` or add complex type tags in `CType.t` to represent
     complex floating-point types (`VT_CFLOAT`, `VT_CDOUBLE`,
     `VT_CLDOUBLE`).
3. **Parser & Expression Evaluation (`tccgen.c`):**
   - Parse complex literal suffixes (`i`, `j`) and complex arithmetic
     operations (`+`, `-`, `*`, `/`, unary `-`).
4. **Code Generation & ABI (`x86_64-gen.c`, etc.):**
   - Generate appropriate ABI-compliant argument passing and return value
     handling for complex structs/pairs (e.g., SSE registers or memory passing
     on x86-64).

## Acceptance Criteria

- **P1:** Parse `_Complex` type specifiers and declare complex variables.
- **P2:** Support complex arithmetic operations and assignments.
- **P3:** Pass gcc complex test suite subset (e.g., complex test cases).
- **Verification:** `./configure && make -j$(nproc) -B && make -j$(nproc) test`
  passes with regression tests added under `tests/tests2/`.
