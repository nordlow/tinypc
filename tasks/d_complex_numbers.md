# Task: D-Style Built-in Complex Numbers and Imaginary Literals

Add support for D's built-in complex and imaginary types (`cfloat`,
`cdouble`, `creal`, `ifloat`, `idouble`, `ireal`) and imaginary literal
syntax (`...i`) in D language mode (`-lang=d`).

## Motivation

Unlike C's `<complex.h>` (which relies on `double _Complex` and library
functions), D features first-class language support for complex and
imaginary numbers, including native imaginary literal suffixes (`1.5i`),
built-in type keywords (`cfloat`, `cdouble`, `creal`, `ifloat`, `idouble`,
`ireal`), and direct arithmetic operators.

Adding built-in complex numbers to TinyPC provides natural mathematical
ergonomics for scientific and numerical computing without relying on
complex header libraries or template wrappers.

---

## Language Specifications

1. **Imaginary and Complex Types**:
   - Pure imaginary types: `ifloat`, `idouble`, `ireal` (representing
     floating-point multiples of the imaginary unit $i$).
   - Complex types: `cfloat`, `cdouble`, `creal` (composed of real and
     imaginary floating-point components).

2. **Imaginary Literals (`i` suffix)**:
   - A numeric literal followed immediately by `i` (e.g., `3.14i`, `42i`)
     denotes a pure imaginary constant expression of type `ireal` or `idouble`.
   - Addition of a real and an imaginary literal (e.g., `1.0 + 2.5i`) forms
     a complex constant.

3. **Native Operators & Conversions**:
   - Standard arithmetic operations (`+`, `-`, `*`, `/`, `==`, `!=`) operate
     natively on complex and imaginary operands according to D arithmetic
     conversion rules.

---

## Type System Design: Pros & Cons Analysis

`VT_BTYPE` currently occupies 4 bits (`0x000f`, values 0–15), and **all 16
possible slots are completely exhausted** (0 through 15 are assigned).
To represent complex and imaginary types (`cfloat`, `cdouble`, `creal`,
`ifloat`, `idouble`, `ireal`), we evaluate two primary design alternatives:

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

1. **Lexer & Tokenization (`tccpp.c`, `tcctok.h`):**
   - Recognize imaginary literal suffixes (`i` / `I`) following numeric
     constants and yield complex/imaginary literal tokens.
2. **Type System Extensions (`tcc.h`):**
   - Introduce `VT_COMPLEX` and `VT_IMAGINARY` modifier flags.
3. **Parser & Code Generation (`tccgen.c`, backends):**
   - Implement type checking and code generation for complex/imaginary
     arithmetic expressions and initializers.
4. **Regression Tests:**
   - Add comprehensive test cases under `tests/tests2/` covering imaginary
     literals, complex arithmetic, and type promotions.
