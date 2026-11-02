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

## Implementation Plan

1. **Lexer & Tokenization (`tccpp.c`, `tcctok.h`):**
   - Recognize imaginary literal suffixes (`i` / `I`) following numeric
     constants and yield complex/imaginary literal tokens.
2. **Type System Extensions (`tcc.h`):**
   - Add built-in complex and imaginary type representations (`VT_CFLOAT`,
     `VT_CDOUBLE`, `VT_CREAL`, `VT_IFLOAT`, `VT_IDOUBLE`, `VT_IREAL`) or map
     them cleanly onto backend float/vector representations.
3. **Parser & Code Generation (`tccgen.c`, backends):**
   - Implement type checking and code generation for complex/imaginary
     arithmetic expressions and initializers.
4. **Regression Tests:**
   - Add comprehensive test cases under `tests/tests2/` covering imaginary
     literals, complex arithmetic, and type promotions.
