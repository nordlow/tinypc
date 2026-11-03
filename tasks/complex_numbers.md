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

## Implementation Strategy: Fine-Grained Incremental Steps

Each step is self-contained, builds successfully, and is verified by its own dedicated regression test under `tests/tests2/`:

| Step | Sub-Step | Action & Scope | Verification Test (`tests/tests2/`) |
| :--- | :--- | :--- | :--- |
| **1. Keywords** | **1a** | Register C99 keywords (`_Complex`, `_Imaginary`). | `NN_c99_keywords.c`: Parses C99 complex keywords without syntax errors. |
| | **1b** | Register D type keywords (`cfloat`, `cdouble`, `creal`, `ifloat`, `idouble`, `ireal`). | `NN_d_keywords.c`: Parses D complex/imaginary type aliases successfully. |
| **2. Type Flags** | **2a** | Define `VT_COMPLEX` and `VT_IMAGINARY` modifier bits in `tcc.h`. | Compile-check header and basic type flag bitwise operations. |
| **3. Literals** | **3a** | Extend lexer numeric suffix parsing for `i`/`j`. | `NN_imaginary_token.c`: Verifies tokens carry correct imaginary suffix flags. |
| | **3b** | Implement constant expression evaluation for pure imaginary literals. | `NN_imaginary_literal.c`: Evaluates compile-time imaginary constants (`2.5i`). |
| **4. Declarations** | **4a** | Parse C99 complex type specifier combinations (`_Complex double`). | `NN_c99_decl.c`: Validates variable declarations, scoping, and `sizeof`. |
| | **4b** | Parse D scalar complex type specifiers (`creal`, `cdouble`, etc.). | `NN_d_decl.c`: Validates D-style complex variable declarations. |
| **5. Operations** | **5a** | Implement real/imaginary component extraction (`__real__`, `__imag__` / `.re`, `.im`). | `NN_complex_parts.c`: Read/write access to real and imaginary components. |
| | **5b** | Implement unary negation and binary addition/subtraction (`+`, `-`). | `NN_complex_add_sub.c`: Complex-complex and complex-scalar arithmetic. |
| | **5c** | Implement binary multiplication and division (`*`, `/`). | `NN_complex_mul_div.c`: Complex multiplication and division rules. |
| | **5d** | Implement equality and inequality comparisons (`==`, `!=`). | `NN_complex_cmp.c`: Exact comparisons of complex values. |
| **6. Gen & ABI** | **6a** | Implement vstack slots, load/store, and temporary allocation. | `NN_complex_storage.c`: Local variable stack frame allocation and spills. |
| | **6b** | Implement function parameter passing and return ABI (registers/stack). | `NN_complex_abi.c`: Cross-function complex passing and GCC test subset. |
