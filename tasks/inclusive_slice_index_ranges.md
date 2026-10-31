# Task: Inclusive Slice Index Ranges (`1..=10`)

## Goal

Extend slice indexing, slicing, and range expressions in TinyPC (TPC) from exclusive upper bounds (`1..10`, representing `[1, 10)`) to support inclusive upper bounds (`1..=10`, representing `[1, 10]`), taking inspiration from Rust and modern language ergonomics.

## Syntax & Semantics

1. **Inclusive Slice Range Syntax:**
   - Syntax: `arr[lo..=hi]`
   - Semantics: Represents the inclusive range from index `lo` through `hi` (`[lo, hi]`), including both `lo` and `hi`.
   - Length of slice: `hi - lo + 1` (when `hi >= lo`).
   - Supports expressions, constants, and `$`: e.g., `arr[0..=$-1]` or `arr[1..=10]`.

2. **Multidimensional Inclusive Slicing:**
   - Syntax: `arr[iS..=iE, jS..=jE]`
   - Semantics: Multi-dimensional inclusive range slicing along axes.

3. **Foreach Range Integration:**
   - Syntax: `foreach (i; lo..=hi)`
   - Semantics: Inclusive integer range iteration from `lo` to `hi` inclusive.

## Design & Implementation Plan

1. **Parser Changes (`tccpp.c`, `tccgen.c`):**
   - Extend token parsing for range operators to recognize `..=` (`TOK_DOTDOT_EQ`).
   - Parse slice range expressions with `..=` into slice descriptors or range nodes with an inclusive flag.
2. **Code Generation & Bounds Checking (`tccgen.c`, `bcheck.c`):**
   - Adjust pointer/bounds checking (`-bt`) for inclusive ranges: validate `0 <= lo <= hi < x.length` (or `hi <= x.length - 1`).
   - Compute slice lengths and pointer arithmetic accordingly (`length = hi - lo + 1`).

## Acceptance Criteria

- **P1:** Support `arr[lo..=hi]` slice indexing and range expressions returning slices with inclusive upper bound `hi`.
- **P2:** Support multidimensional inclusive slicing (`A[iS..=iE, jS..=jE]`) and `foreach (i; lo..=hi)` inclusive loops.
- **P3:** Full integration with bounds checking (`-bt`) and D-style safety/purity checks.
- **Verification:** `./configure && make -j$(nproc) -B && make -j$(nproc) test` passes with new regression tests under `tests/tests2/`.
