# Task: Multi-Dimensional Arrays with Indexing (`[i, j, k, ...]`) and Slicing (`[iS..iE, jS..jE, ...]`)

## Goal

Add native support for multi-dimensional array indexing and slicing in
TinyPC (TPC), supporting comma-separated multi-index access
(`A[i, j, k]`) returning elements and comma-separated range slicing
(`A[iS..iE, jS..jE]` half-open or `A[iS..=iE, jS..=jE]` inclusive) returning sub-spans or multi-dimensional slices.
Take inspiration from Mojo and Julia syntax and ergonomics.

## Syntax & Semantics

1. **Multi-Index Indexing:**
   - Syntax: `arr[i, j, k, ...]`
   - Semantics: Flattens or computes strided multi-dimensional index
     offset: $\text{offset} = i \cdot s_0 + j \cdot s_1 + k \cdot s_2 + \dots$,
     where $s_x$ are compile-time or runtime stride dimensions.
   - Works on static multi-dimensional arrays (`T[N1][N2][N3]`) and
     dynamic/slice multi-dimensional views.

2. **Multi-Dimensional Slicing:**
   - Syntax: `arr[iS..iE, jS..jE, ...]` (half-open) and `arr[iS..=iE, jS..=jE, ...]` (inclusive upper limit).
   - Semantics: Returns a multi-dimensional slice (or strided view)
   - consisting of bounds `[iS..iE)` or `[iS..=iE]` along axis 0, etc.
   - Integrates with TPC's existing slice system (`T[..]`) and bounds
     checking (`-bt`).

## Design & Implementation Plan

1. **Parser Changes (`tccpp.c`, `tccgen.c`):**
   - Extend bracket expression parsing (`[...]`) to recognize comma-separated
   - expressions and range expressions (`..` and `..=`).
   - Distinguish single-index/slice from multi-dimensional comma-separated
     index/slice tuples.
2. **Type Representation (`tcc.h`, `tccgen.c`):**
   - Support multi-dimensional array types and multi-dimensional slice
     descriptors (storing dimensions, shapes, and strides).
3. **Code Generation (`x86_64-gen.c`, `tccgen.c`):**
   - Emit efficient address calculation code for strided multi-dimensional
     indexing and slice descriptor construction.
4. **Bounds Checking & Safety (`bcheck.c`):**
   - Extend pointer and bounds checking (`-bt`) to validate indices and
     slice ranges across all dimensions.

## Acceptance Criteria

- **P1:** Support `A[i, j]` and `A[i, j, k]` indexing on multi-dimensional
  arrays and slices, correctly computing strided addresses and returning
  elements.
- **P2:** Support `A[iS..iE, jS..jE]` and `A[iS..=iE, jS..=jE]` slicing, returning a valid multi-dimensional
  slice view with correct bounds and strides.
- **P3:** Full integration with bounds checking (`-bt`) and D-style safety/purity checks.
- **Verification:** `./configure && make -j$(nproc) -B && make -j$(nproc) test`
  passes with new regression tests under `tests/tests2/`.
