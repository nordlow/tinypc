# Task: D-Style Slice Range Checking and Bounds Optimization

Add run-time bounds checking for D-style slice indexing (`x[i]`) and
slicing (`x[lo..hi]` half-open and `x[lo..=hi]` inclusive upper limit), and
optimize out checks for provably safe index expressions such as `x[$/2]` and `x[$/3]`.

## Goal

- **P1 (Runtime Bounds Checking):** When `-b` (`CONFIG_TCC_BCHECK`) is
  enabled, validate slice indexing `x[i]` and slicing (`x[lo..hi]` or
  `x[lo..=hi]`) against the slice's `.length` at run time, replacing raw
  pointer arithmetic with checked slice boundaries, given `x` of `T[...]`
  for any type `T`.
- **P2 (Bounds Check Optimization):** Statically identify and omit
  runtime bounds checks for provably safe index expressions (e.g.,
  `0`, `$/2`, `$/3`, `$*2/3`, `$/4`, `$*2/4`, `$*3/4`, etc, and static
  constants within `[0, $ - 1]`) even when `-b` is active.

## Motivation

- **Correctness & Safety:** Slices (`T[...]`) are fat pointers
  containing both a pointer and a length.  Checking against the
  underlying buffer allocation (via existing C pointer bounds
  checking) allows out-of-bounds accesses relative to the slice's
  actual `.length`.
- **Performance (Zero-Cost Safe Indices):** Expressions like `x[$/2]`
  and `x[$/3]` are mathematically guaranteed to be within bounds for
  any non-empty slice (`$ >= 1`), so emitting runtime bounds checks
  for them is redundant.

## Semantics & Rules

1. **Slice Indexing (`x[i]`):**
   - Under `-b`, generate a check: `0 <= i < x.length`.
   - If `i` is out of bounds, invoke `bound_error()`.
2. **Slice Slicing (`x[lo..hi]` and `x[lo..=hi]`):**
   - Under `-b`, generate checks: `0 <= lo <= hi < x.length` (inclusive `lo..=hi`) or `0 <= lo <= hi <= x.length` (half-open `lo..hi`).
3. **Optimizations (Safe Index Omission):**
   - **Constant Indices:** If `i` is a constant integer `c` and slice length
     `$` is a constant or known, check at compile time or omit if valid.
   - **Divisions by 2 or 3 (`$/2`, `$/3`):** For any unsigned or positive
     integer `$` (`length`), integer division by `2` or `3` yields an index
     strictly satisfying `0 <= $ / k < $` for all `$` $\ge 1$.  Omit the
     runtime bounds check for `$/2` and `$/3`.
   - **Lower Bound (`0`):** `x[0]` is omitted when slice is non-empty or
     handled by standard checks.

## Implementation Details (`tccgen.c`)

- Reuse `tcc_state->do_bounds_check` and `bound_error()`.
- Add slice bounds-check emission in `[` postfix handling (`tccgen.c:7615`):
  - For indexing: load slice `.length`, compare index `i` against it.
  - For range slicing (`lo..hi` and `lo..=hi`): validate `lo`, `hi`, and range width.
- Add an analysis helper `is_provably_safe_slice_index()` to detect `0`,
  `$/2`, `$/3`, and compile-time constants within bounds.

## Acceptance Criteria

- `-b` enables runtime bounds checking for slice indexing and slicing
  (`lo..hi` and `lo..=hi`) against slice `.length`.
- `x[$/2]`, `x[$/3]`, and `x[0]` generate no redundant runtime bounds
  checks under `-b`.
- Out-of-bounds slice indexing/slicing triggers a clean runtime error
  via `bound_error()`.
- Verification: `./configure && make -j$(nproc) -B && make -j$(nproc) test`
  passes.
