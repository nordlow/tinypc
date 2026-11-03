# Task: Support D-Style Scalar Types in `-lang=d` Mode

> **Status:** Proposed feature specification.

Support D-style scalar types when `-lang=d` mode is enabled, providing full
syntactic and semantic parity for D primitive and scalar type names in TinyPC
(TPC).

## Goal

Enable D-specific scalar type keywords and aliases in `-lang=d` mode so that
D source files and D-style code idioms compile natively without requiring C
primitive aliases.

## Supported D Scalar Types

1. **Exact-Width / Integral Types:**
   -  `byte` (signed 8-bit integer)
   -  `ubyte` (unsigned 8-bit integer)
   -  `short` (signed 16-bit integer)
   -  `ushort` (unsigned 16-bit integer)
   -  `int` (signed 32-bit integer)
   -  `uint` (unsigned 32-bit integer)
   -  `long` (signed 64-bit integer)
   -  `ulong` (unsigned 64-bit integer)
   -  `cent` (signed 128-bit integer)
   -  `ucent` (unsigned 128-bit integer)

2. **Character & String Types:**
   -  `char` (unsigned 8-bit UTF-8 code unit)
   -  `wchar` (unsigned 16-bit UTF-16 code unit)
   -  `dchar` (unsigned 32-bit UTF-32 code unit)

3. **Boolean Type:**
   -  `bool` (1-byte boolean type)

4. **Floating-Point & Extended Precision:**
   -  `float` (32-bit IEEE float)
   -  `double` (64-bit IEEE double)
   -  `real` (extended precision floating-point, e.g. 80-bit x87 or 128-bit float)

5. **Imaginary & Complex Scalar Types:**
   -  `ifloat`, `idouble`, `ireal` (imaginary types)
   -  `cfloat`, `cdouble`, `creal` (complex types)

## Implementation Details

1. **Lexer & Keywords (`tcctok.h`, `tccpp.c`):**
   - [ ] Gate D scalar type keywords (`byte`, `ubyte`, `cent`, `ucent`, `dchar`,
     etc.) behind `tcc_state->lang_d`.
2. **Type System Mapping (`tcc.h`, `tccgen.c`):**
   - [ ] Map D scalar types to underlying TinyPC basic type flags (`VT_BYTE`,
     `VT_SHORT`, `VT_INT`, `VT_LLONG`, `VT_QLONG`, `VT_BOOL`, `VT_FLOAT`,
     `VT_DOUBLE`, `VT_LDOUBLE`, etc.) with correct signed/unsigned and UTF
     width properties.
3. **Diagnostics & Error Handling:**
   - [ ] Issue helpful diagnostics if D scalar types are used outside of
     `-lang=d` mode.

## Acceptance Criteria

- [ ] **P1:** Register D scalar type tokens and map them in the type parser when
  `-lang=d` is active.
- [ ] **P2:** Correctly handle sizeof, conversions, arithmetic, and ABI
  marshaling for all D scalar types (`cent`, `ucent`, `dchar`, `wchar`, `bool`,
  etc.).
- [ ] **Verification:** `./configure && make -j$(nproc) -B && make -j$(nproc) test`
  passes with new regression tests added under `tests/tests2/`.
