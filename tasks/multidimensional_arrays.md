# Task: Multi-Dimensional Arrays with Indexing (`[i, j, k, ...]`) and Slicing (`[iS..iE, jS..jE, ...]`)

## Goal

Add native support for multi-dimensional array indexing and slicing in
TinyPC (TPC), supporting comma-separated multi-index access (`A[i, j,
k]`) returning elements, comma-separated range slicing (`A[iS..iE,
jS..jE]` half-open or `A[iS..=iE, jS..=jE]` inclusive), Julia-style
full-axis colons (`A[i, j, :]`), and Unicode subscript indexing
(`Aᵢⱼ₉`).  Take inspiration from Mojo and Julia syntax and
ergonomics.

## Syntax & Semantics

1. **Multi-Index Indexing:**
   - Syntax: `arr[i, j, k, ...]`
   - Semantics: Flattens or computes strided multi-dimensional index
     offset: $\text{offset} = i \cdot s_0 + j \cdot s_1 + k \cdot s_2 + \dots$,
     where $s_x$ are compile-time or runtime stride dimensions.
   - Works on static multi-dimensional arrays (`T[N1][N2][N3]`) and
     dynamic/slice multi-dimensional views.

2. **Multi-Dimensional Slicing:**
   - Syntax: `arr[iS..iE, jS..jE, ...]` (half-open),
     `arr[iS..=iE, jS..=jE, ...]` (inclusive), and full-axis colons
     (`arr[i, :, k]`).
   - Semantics: Returns a multi-dimensional slice or strided view
     spanning bounds `[iS..iE)` or entire axes for `:`.
- Integrates with TPC's existing slice system (`T[..]`) and bounds
  checking (`-bt`).
- **Named Parameters / Arguments Compatibility:** Both `:` and `..` syntax
  inside indexing brackets `[...]` will be allowed as long as they do not
  clash with named parameters or keyword arguments (if introduced).


3. **Subscript / Unicode Indexing (`Aᵢⱼ₉`):**
   - Syntax: Identifiers and indexing expressions incorporating Unicode
     subscript characters (e.g., `₀₋₉`, `ₐ`, `ᵢ`, `ⱼ`, `ₖ`).
   - Semantics: Lexical and identifier normalization mapping subscript
     characters to standard indices or variable components for
     mathematical conciseness inspired by Julia/APL.

## Design & Implementation Plan

1. **Parser Changes (`tccpp.c`, `tccgen.c`):**
   - Extend bracket expression parsing (`[...]`) to recognize
     comma-separated expressions and range expressions (`..` and
     `..=`).
   - Distinguish single-index/slice from multi-dimensional
     comma-separated index/slice tuples.
2. **Type Representation (`tcc.h`, `tccgen.c`):**
   - Support multi-dimensional array types and multi-dimensional slice
     descriptors (storing dimensions, shapes, and strides).
3. **Code Generation (`x86_64-gen.c`, `tccgen.c`):**
   - Emit efficient address calculation code for strided
     multi-dimensional indexing and slice descriptor construction.
- Extend pointer and bounds checking (`-bt`) to validate indices and
  slice ranges across all dimensions.

## Memory Layouts (NumPy, Julia, Mojo Inspiration)

Different ecosystem paradigms handle multi-dimensional array memory layout
differently:

1. **NumPy (C-contiguous / Row-Major):**
   - The last dimension varies fastest in memory. Stride for dimension $i$
     decreases from left to right. Ideal for C-language interop and row-wise
     traversal.
2. **Julia (Fortran-contiguous / Column-Major):**
   - The first dimension varies fastest in memory. Stride for dimension $i$
     increases from left to right. Ideal for mathematical and linear algebra
     computations (BLAS/LAPACK interop).
3. **Mojo (Explicit Strides & Custom Layouts):**
   - Allows arbitrary compile-time or runtime strides and layout qualifiers
     enabling zero-copy transposes and strided slices.

### Convenient Expression in TPC

To express these layouts cleanly without boilerplate:
- **Layout Attributes:** Support `@`-attribute layout specifiers on array and
  slice types (e.g., `@layout(row) int[N, M]` or `@layout(col) int[N, M]`).
- **Easy Switchability & Zero-Copy Transpose:** Layouts must be easily
  switchable between row-major and column-major orders without copying data,
  either via attribute annotations or view-casting methods (e.g.
  `.layout(row)` / `.layout(col)`).
- **Standard Strided View Descriptor:** Represent multi-dimensional views as
  compact runtime descriptors containing pointer, shape array, and stride
  array, allowing zero-copy slicing and seamless interop with NumPy C-API
  and Julia arrays.

## Acceptance Criteria

- **P1:** Support `A[i, j]` and `A[i, j, k]` indexing on
  multi-dimensional arrays and slices, correctly computing strided
  addresses and returning elements.
- **P2:** Support `A[iS..iE, jS..jE]` and `A[iS..=iE, jS..=jE]`
  slicing, returning a valid multi-dimensional slice view with correct
  bounds and strides.
- **P3:** Full integration with bounds checking (`-bt`) and D-style
  safety/purity checks.
- **Verification:** `./configure && make -j$(nproc) -B && make -j$(nproc) test`
  passes with new regression tests under `tests/tests2/`.
