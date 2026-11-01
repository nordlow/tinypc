# Encapsulate Bounded String Buffers into Structs (`CString` / `StringSlice`)

> **Status:** Proposed refactoring task.

Refactor raw `(char *buf, size_t buf_size)` passing across compiler utility
functions (`pstrcpy`, `pstrcat`, `pstrncpy`) to reuse and unify around
existing codebase string buffer types (`CString` and `StringSlice` defined in
`tcc.h`), drawing inspiration from advanced D-style fat pointer slice layouts
(such as those in `blitz/str.h` and `blitz/fstr.h`).

## Motivation

Currently, TinyPC codebase occasionally relies on raw pointer and size pairs
(`char *buf, size_t buf_size`) or legacy string helpers alongside existing
structured buffer types:
- **`StringSlice`**: Non-owning immutable string slice view (`ptr`, `len`).
- **`CString`**: Dynamic string buffer descriptor (`size`, `size_allocated`,
  `data`).

Additionally, referencing design patterns from `blitz` (`str.h` / `fstr.h`),
we can enrich string slices with metadata bitfields (such as `isNullTerm`,
`isASCII`, `isImmutable`) mimicking D-style fat pointers and ensuring memory
safety and high performance.

## Proposed Design

1. **Leverage and Extend Existing Structs (`tcc.h`):**
   - Use `CString` for mutable, growing or bounded buffers.
   - Use `StringSlice` (optionally enhanced with bitfield flags from blitz)
     for read-only string slices and views.
2. **Method-like / Helper API Refactoring:**
   - Adapt string copy/cat utilities to accept `CString *` or bounded buffers
     rather than raw `char *buf, size_t buf_size` pairs.
3. **Integration Across Compiler:**
   - Refactor high-traffic string formatting and path manipulation sites in
     `libtcc.c`, `tccgen.c`, and `tccpp.c`.

## Acceptance Criteria

- **P1:** Update string manipulation utility functions to integrate with
  `CString` and `StringSlice`.
- **P2:** Refactor call sites using raw `(char *buf, size_t buf_size)` to
  use existing struct definitions.
- **Verification:** `./configure && make -j$(nproc) -B && make -j$(nproc) test`
  passes with zero regressions.
