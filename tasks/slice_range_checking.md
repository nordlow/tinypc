# Task: Slice Range Checking and Bounds Optimization

Add run-time bounds checking for D-style slice indexing (`x[i]`) and
slicing (`x[lo..hi]` half-open and `x[lo..=hi]` inclusive upper
limit), and optimize out checks for provably safe index expressions
such as `x[$/2]` and `x[$/3]`.

## Goal

- [x] **P1 (Runtime Bounds Checking):** In D mode by default (and
      under `-b` in C mode), validate slice indexing `x[i]` and
      slicing (`x[lo..hi]` or `x[lo..=hi]`) against the slice's
      `.length` at run time, replacing raw pointer arithmetic with
      checked slice boundaries, given `x` of `T[...]` for any type
      `T`.  `-fno-slice-bounds-check` disables the checks.
- [x] **P2 (Bounds Check Optimization):** Statically identify and
      simplify runtime bounds checks for provably safe index
      expressions (e.g., `$/2`, `$/3`, `$/4`, and their parenthesized
      forms) even when `-b` is active.  The index/length comparison is
      omitted; a cheaper `length != 0` guard remains because such an
      index is only provably in bounds for a non-empty slice.
- [x] **P3 (Safe `.length` Mutability Restrictions):** In `@safe`
      code, direct modification of a slice's `.length` property (e.g.,
      `x.length = ...` or `x.length += ...`) is a compilation error
      with descriptive diagnostics.  *Note:* Slices can be safely and
      efficiently sub-sliced (split) (typically in recursive
      algorithms like Quicksort) via the pattern `x[0..$*P/Q]` where
      `P` and `Q` are integer compile-time expressions with `P <= Q`.

## Motivation

-  **Correctness & Safety:** Slices (`T[...]`) are fat pointers
  containing both a pointer and a length.  Checking against the
  underlying buffer allocation (via existing C pointer bounds
  checking) allows out-of-bounds accesses relative to the slice's
  actual `.length`.
-  **Performance (Zero-Cost Safe Indices):** Expressions like `x[$/2]`
  and `x[$/3]` are mathematically guaranteed to be within bounds for
  any non-empty slice (`$ >= 1`), so emitting runtime bounds checks
  for them is redundant.

## Semantics & Rules

1. **Slice Indexing (`x[i]`):**
   -  Under `-b`, generate a check: `0 <= i < x.length`.
   -  If `i` is out of bounds, invoke `bound_error()`.
2. **Slice Slicing (`x[lo..hi]` and `x[lo..=hi]`):**
   -  Under `-b`, generate checks: `0 <= lo <= hi < x.length` (inclusive `lo..=hi`) or `0 <= lo <= hi <= x.length` (half-open `lo..hi`).
3. **Optimizations (Safe Index Simplification):**
   -  **Constant Indices:** If `i` is a constant integer `c` and slice length
     `$` is a constant or known, check at compile time or omit if valid.
   -  **Divisions by k (`$/k`, `k >= 2`):** For any positive
     integer `$` (`length`), integer division by `k` yields an index
     strictly satisfying `0 <= $ / k < $` for all `$ >= 1`.  The
     index/length comparison is replaced by a `length != 0` guard;
     omitting even that guard would be unsound for an empty slice.
   -  **Lower Bound (`0`):** `x[0]` keeps the ordinary check, because
     the slice must be non-empty.

## Implementation Details (`tccgen.c`)

- [x] Use a dedicated `slice_bounds_check` flag (auto by default: on in
  D mode, otherwise follows `do_bounds_check`) with runtime helpers in
  `lib/libtcc1.c` that report the source location, index or range, and
  length.  The existing allocation-based `bound_error()` cannot see
  slice lengths.
- [x] Add slice bounds-check emission in `[` postfix handling (`tccgen.c:7615`):
  - [x] For indexing: load slice `.length`, compare index `i` against it.
  - [x] For range slicing (`lo..hi` and `lo..=hi`): validate `lo`, `hi`, and range width.
- [x] Add a token-level analysis helper (`subscript_safe_divisor()`)
  that recognizes `$/k` and emits only the non-empty guard
  (`gen_slice_nonempty_check()`).  Full omission is not sound for
  `k = 1` or for an empty slice; `$*P/Q` forms are deliberately left
  checked until the overflow behaviour of the multiplication is
  specified.

## Acceptance Criteria

- [x] D mode enables runtime bounds checking for slice indexing and
  slicing (`lo..hi` and `lo..=hi`) against slice `.length`; in C mode
  the same checks are enabled by `-b`.
- [x] `x[$/2]`, `x[$/3]`, and `x[$/4]` do not compare the computed
  index against `.length`; they guard `length != 0` instead.  `x[0]`
  keeps the ordinary check, since an empty slice must still trap.
- [x] Out-of-bounds slice indexing/slicing triggers a clean runtime error
  naming the source location, the index or range, and the length.
- [x] Verification: `./configure --ninja && ninja -C build check`
  passes, and `ninja -C build sani` shows no new failures.
