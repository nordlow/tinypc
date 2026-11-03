# Task: Unified C99 & D Complex/Imaginary Numbers and Literals

Implement unified support for C99 complex and imaginary numbers
(`_Complex float`, `_Complex double`, `_Complex long double`,
`_Imaginary`, and `<complex.h>`) along with D's built-in
complex/imaginary types (`cfloat`, `cdouble`, `creal`, `ifloat`,
`idouble`, `ireal`) and imaginary literal suffix syntax (`...i`),
using the **Modifier Flag Combination** architecture.

## Motivation

Both C99 and D require robust, first-class language support for complex
and imaginary numbers:
- **C99 Standard:** Requires `_Complex` and `_Imaginary` type
  specifiers and standard header support via `<complex.h>`.
- **D Language:** Features built-in keywords (`cfloat`, `creal`, etc.)
  and native imaginary literal suffix syntax (`1.5i`), providing
  natural mathematical ergonomics for scientific and numerical
  computing without relying on templates or external helper libraries.

---

## Language Specifications

1. **C99 Complex & Imaginary Types**:
   - `_Complex float`, `_Complex double`, `_Complex long double` (and
     macro aliases `float complex`, `double complex`, `long double
     complex` from `<complex.h>`).
   - `_Imaginary` types and associated macros.

2. **D Built-in Complex & Imaginary Types**:
   - Pure imaginary types: `ifloat`, `idouble`, `ireal`
     (floating-point multiples of $i$).
   - Complex types: `cfloat`, `cdouble`, `creal` (composed of real and
     imaginary parts).

3. **Imaginary Literals & Suffixes**:
   - Numeric literals followed by `i` or `j` (e.g., `3.14i`, `42.0j`)
     denote pure imaginary constant expressions.
   - Real + imaginary addition (e.g., `1.0 + 2.5i`) forms a complex
     constant.

4. **Native Operators & Conversions**:
   - Standard arithmetic operations (`+`, `-`, `*`, `/`, `==`, `!=`)
     operate natively on complex and imaginary operands.

---

## Type System Design: Modifier Flag Combination

Because `VT_BTYPE` currently occupies all 4 available bits (`0x000f`,
values 0–15), widening `VT_BTYPE` would collide with `VT_UNSIGNED`
(`0x0010`) and require a cascading renumbering of all subsequent bits
and compiler switch statements.

Instead, we use **Approach B (Modifier Flag Combination)**:
- **Core Strategy:** Keep `VT_BTYPE` unchanged (`VT_FLOAT`,
  `VT_DOUBLE`, `VT_LDOUBLE`) and introduce dedicated modifier flags
  (`VT_COMPLEX` and `VT_IMAGINARY`) in the type modifier bit range.
- **Advantages:**
  - **Zero Bit Collision:** Avoids shifting existing type masks, basic
    types, or storage specifiers.
  - **Standard Alignment:** Mirrors C99 (`float _Complex`) where
    complex types are floating-point base types combined with a
    complex attribute flag.
  - **Code Reuse:** Real-part extraction, promotion rules, and backend
    float generation share code with standard floating-point
    operations.

---

## Implementation Strategy: Minimal-Sized Incremental Steps

Each step is self-contained, builds successfully, and is verified by
its own dedicated regression test under `tests/tests2/`:

1. **Step 1: Keyword & Token Registration (`tcctok.h`, `tccpp.c`)**
   - **Action:** Register `_Complex`, `_Imaginary`, and D type
     keywords (`cfloat`, `cdouble`, `creal`, `ifloat`, `idouble`,
     `ireal`).
   - **Verification Test:** Add a test verifying keywords parse
     successfully in C99 and D modes without syntax errors.

2. **Step 2: Type Modifier Flags (`tcc.h`)**
   - **Action:** Define `VT_COMPLEX` and `VT_IMAGINARY` modifier flags
     in the type qualifier/modifier bit range.
   - **Verification Test:** Verify header compilation and basic type
     descriptor construction.

3. **Step 3: Imaginary Literal Suffixes (`...i`, `...j`)**
   - **Action:** Extend the numeric lexer/parser to recognize `i` and
     `j` suffixes on numeric literals, producing pure imaginary
     constants.
   - **Verification Test:** Add `tests/tests2/NN_imaginary_literal.c`
     verifying constant evaluation of imaginary literals (`2.5i`,
     `1.0j`).

4. **Step 4: Complex Type Parsing & Declaration (`tccgen.c`)**
   - **Action:** Update `parse_btype()` and declaration logic to
     handle complex/imaginary type specifiers, binding them to
     underlying float types with `VT_COMPLEX` / `VT_IMAGINARY` flags.
   - **Verification Test:** Add `tests/tests2/NN_complex_decl.c`
     verifying variable declarations (`_Complex double z;`, `creal
     c;`) and `sizeof`.

5. **Step 5: Complex Arithmetic & Operations (`tccgen.c`)**
   - **Action:** Implement type conversion, promotion, and binary
     arithmetic operations (`+`, `-`, `*`, `/`, `==`, `!=`, unary `-`)
     for complex/imaginary operands.
   - **Verification Test:** Add `tests/tests2/NN_complex_arith.c`
     verifying complex calculations and comparisons.

6. **Step 6: Code Generation & ABI (`x86_64-gen.c`, etc.)**
   - **Action:** Implement ABI-compliant argument passing and return
     value handling for complex pairs (SSE registers or memory passing
     on x86-64).
   - **Verification Test:** Add `tests/tests2/NN_complex_abi.c` and
     pass the gcc complex test suite subset.
