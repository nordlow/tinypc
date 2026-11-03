# Task: Automated In-Place Syntax Refactoring Pass (`tpc --refactor`)

## Goal

Implement an automated file in-place refactoring pass in TinyPC (TPC)
to modernize legacy C constructs into preferred D-style idiomatic
constructs:
-  C-style arrays (`int arr[N]`) $\to$ D-style type-first arrays (`int[N] arr`)
-  C-style indexing/counting `for` loops $\to$ D-style `foreach` and `foreach_reverse`

## Refactoring Transformations

1. **Type-First Array Syntax:**
   -  Match: `T var[N]` (variable definitions, function parameters, typedefs).
   -  Replace: `T[N] var`.
   -  Preserves array sizes, qualifiers (`const`, `immutable`, `volatile`), and multi-dimensional shapes.

2. **Loop Modernization (`for` $\to$ `foreach` / `foreach_reverse`):**
   -  Match: C `for` loops of the form:
     `for (int i = 0; i < N; ++i)` or `for (int i = N - 1; i >= 0; --i)` iterating over an array or range.
   -  Replace: D-style `foreach (i; 0..N)` or `foreach_reverse (i; 0..N)`.

## CLI Interface & Safety

-  Command: `tpc --refactor=<rule> <file.c>` or `tpc --refactor-all <dir>`
-  Safety: Dry-run mode (`--refactor-dry-run`) showing git-style diffs before modifying files in-place; refuses to refactor files with compiler diagnostics errors.

## Acceptance Criteria

- [ ] **P1:** Automated conversion of C array declarations to D type-first syntax.
- [ ] **P2:** Automated conversion of monotonic counting `for` loops to `foreach` and `foreach_reverse`.
- [ ] **P3:** Dry-run diff generation and in-place file safety guarantees.
- [ ] **Verification:** `./configure && make -j$(nproc) -B && make -j$(nproc) test` passes with refactoring integration tests under `tests/tests2/`.
