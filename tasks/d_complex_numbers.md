# Task: Unified C99 & D Complex/Imaginary Numbers and Literals

Implement unified support for C99 complex and imaginary numbers
(`_Complex float`, `_Complex double`, `_Complex long double`, `_Imaginary`,
and `<complex.h>`) along with D's built-in complex/imaginary types
(`cfloat`, `cdouble`, `creal`, `ifloat`, `idouble`, `ireal`) and imaginary
literal suffix syntax (`...i`).

## Motivation

Both C99 and D require robust, first-class language support for complex
and imaginary numbers:
- **C99 Standard:** Requires `_Complex` and `_Imaginary` type specifiers
  and standard header support via `<complex.h>`.
- **D Language:** Features built-in keywords (`cfloat`, `creal`, etc.) and
  native imaginary literal suffix syntax (`1.5i`), providing natural mathematical
  ergonomics for scientific and numerical computing without relying on
  templates or external helper libraries.

---

## Language Specifications

1. **C99 Complex & Imaginary Types**:
   - `_Complex float`, `_Complex double`, `_Complex long double` (and macro
     aliases `float complex`, `double complex`, `long double complex` from
     `<complex.h>`).
   - `_Imaginary` types and associated macros.

2. **D Built-in Complex & Imaginary Types**:
   - Pure imaginary types: `ifloat`, `idouble`, `ireal` (floating-point
     multiples of $i$).
   - Complex types: `cfloat`, `cdouble`, `creal` (composed of real and
     imaginary parts).

3. **Imaginary Literals & Suffixes**:
   - Numeric literals followed by `i` or `j` (e.g., `3.14i`, `42.0j`) denote
     pure imaginary constant expressions.
   - Real + imaginary addition (e.g., `1.0 + 2.5i`) forms a complex constant.

4. **Native Operators & Conversions**:
   - Standard arithmetic operations (`+`, `-`, `*`, `/`, `==`, `!=`) operate
     natively on complex and imaginary operands.

---

## Type System Design: Pros & Cons Analysis

`VT_BTYPE` currently occupies 4 bits (`0x000f`, values 0–15), and **all 16
possible slots are completely exhausted** (0 through 15 are assigned).
To represent complex and imaginary types, we evaluate two primary design
alternatives:

### Approach A: Extending `VT_BTYPE` (New Dedicated Basic Type Tags)
Expand `VT_BTYPE` width (e.g., to 5 or 6 bits) and add new basic type tags
(`VT_CFLOAT`, `VT_CDOUBLE`, `VT_CREAL`, `VT_IFLOAT`, `VT_IDOUBLE`,
`VT_IREAL`).

- **Pros:**
  - Every complex/imaginary type has a distinct, standalone basic type tag
    (`t & VT_BTYPE == VT_CREAL`).
  - Straightforward type matching in switch statements.
- **Cons:**
  - **Bit Squeeze:** Widening `VT_BTYPE` collides with `VT_UNSIGNED` (`0x0010`),
    forcing a cascading renumbering of all subsequent modifier and storage
    flags (`VT_UNSIGNED`, `VT_DEFSIGN`, `VT_ARRAY`, `VT_BITFIELD`, etc.).
  - **Audit Burden:** Requires auditing every bitmask operation and backend
    switch statement across the compiler.

### Approach B: Modifier Flag Combination (Recommended)
Keep `VT_BTYPE` as-is (`VT_FLOAT`, `VT_DOUBLE`, `VT_LDOUBLE`) and introduce
dedicated modifier flags (`VT_COMPLEX` and `VT_IMAGINARY`) in the modifier
bit range.

- **Pros:**
  - **Zero Bit Collision:** Avoids shifting existing type masks or storage
    flags.
  - **Standard Alignment:** Mirrors C99 (`float _Complex`) and general
    compiler design: complex/imaginary types are fundamentally floating-point
    base types modified by a complex or imaginary attribute.
  - **Code Reuse:** Real-part extraction, promotion rules, and backend
    float generation share code with standard floating-point operations.
- **Cons:**
  - Requires checking both `VT_BTYPE` and the modifier flag (`(t & VT_BTYPE) ==
    VT_DOUBLE && (t & VT_COMPLEX)`) when identifying complex types.

---

## Implementation Plan

1. **Lexer & Keyword Support (`tcctok.h`, `tccpp.c`):**
   - Register `_Complex`, `_Imaginary`, and D type keywords (`cfloat`,
     `creal`, `ifloat`, etc.).
   - Recognize imaginary literal suffixes (`i`, `j`) following numeric
     constants and yield complex/imaginary literal tokens.
2. **Type System Extensions (`tcc.h`):**
   - Introduce `VT_COMPLEX` and `VT_IMAGINARY` modifier flags.
3. **Parser & Expression Evaluation (`tccgen.c`):**
   - Parse complex/imaginary literal expressions and arithmetic operations
     (`+`, `-`, `*`, `/`, unary `-`).
4. **Code Generation & ABI (`x86_64-gen.c`, etc.):**
   - Generate ABI-compliant argument passing and return value handling for
     complex pairs (SSE registers or memory passing on x86-64).
5. **Regression Tests:**
   - Pass gcc complex test suite subset and add comprehensive test cases
     under `tests/tests2/`.
