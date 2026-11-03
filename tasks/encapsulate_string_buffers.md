# Encapsulate Bounded String Buffers into Owning Container and View/Slice Structs

> **Status:** Proposed refactoring task.

Refactor raw `(char *buf, size_t buf_size)` passing across compiler
utility functions (`pstrcpy`, `pstrcat`, `pstrncpy`) to reuse and
unify around existing codebase string buffer types (`CString` and
`StringSlice` defined in `tcc.h`), drawing inspiration from advanced
D-style fat pointer slice layouts (such as those in `blitz/str.h` and
`blitz/fstr.h`).

## Motivation

Currently, TinyPC occasionally relies on raw pointer and size pairs
(`char *buf, size_t buf_size`) or legacy string helpers alongside
existing structured buffer types:

- `StringSlice`: non-owning immutable string slice view (`ptr`,
  `len`). Should be replaced merged with `str_t` in
  `blitz/str.h`. Consider unifying on the name `StrV` or `StrSlice`.
- `CString`: dynamic string buffer descriptor (`size`,
  `size_allocated`, `data`). Should be merged with with `FStr` in
  `blitz/ftr.h`. Should be qualified `__attribute((nocopy))`.

Referencing design patterns from `blitz` (`str.h` / `fstr.h`), string
slices can be enriched with metadata bitfields (such as `isNullTerm`,
`isASCII`, `isImmutable`) mimicking D-style fat pointers, improving
memory safety and performance.

## Goal

- Eliminate raw pointer/size pairs at high-traffic call sites.
- Make the null-termination and capacity invariants explicit in the
  type.
- Keep the change mechanical and behaviour-preserving.

## Proposed design

1. **Leverage and extend existing structs (`tcc.h`).**
   - `CString` for mutable, growing or bounded buffers.
   - `StringSlice` (optionally with bitfield flags) for read-only views.
2. **Helper API.**
   - `cstring_append(CString *, const char *)`
   - `cstring_appendf(CString *, const char *, ...)`
   - `cstring_truncate(CString *, size_t)`
   - `slice_from_cstr(const char *)`, `slice_eq`, `slice_starts_with`
   - `pstrcpy`/`pstrcat`/`pstrncpy` wrappers that take structs and keep
     the existing raw variants for compatibility.
3. **Call-site migration.**
   - `libtcc.c`, `tccgen.c`, `tccpp.c`, `tccdbg.c`, `tccelf.c`.
   - Prefer changing one subsystem per commit.

## Implementation phases

### Phase 1 — Definitions and invariants

- Audit the current `CString`/`StringSlice` definitions in `tcc.h`.
- Document for each:
  - who owns `data`,
  - whether `data` is NUL-terminated,
  - whether `len` includes the NUL,
  - what happens on allocation failure.
- Add `assert()`s in debug builds for these invariants.

### Phase 2 — Helper functions

- Add the helpers next to the existing string utilities.
- Keep them allocation-failure-safe: return 0/`NULL` rather than
  aborting, matching the existing utility style.
- Unit-test the helpers via a small `tests/libtcc_test.c` case or a
  dedicated test.

### Phase 3 — Migrate call sites

- Start with functions whose buffer is already a `CString` field.
- Convert `snprintf`-style bounded writes to `cstring_appendf`.
- Leave `pstrcpy` callers where the destination is a stack array; add
  a `slice_from_buf` only if it removes more code than it adds.

### Phase 4 — Cleanup

- Remove now-unused raw helpers only after all callers migrate and
  the build has no warnings.
- Do not introduce a new allocator or change allocation sizes.

## Edge cases

- Empty strings and `len == 0`.
- `NULL` data with non-zero length (must never happen; assert).
- Reallocation invalidating outstanding `StringSlice`s.
- Embedded NUL bytes: `len`-based views must still work.
- UTF-8 vs ASCII flag: do not trust a stale `isASCII` bit after a
  mutation.
- `isNullTerm` must be recomputed, never copied blindly.

## Test plan

| check | asserts |
| --- | --- |
| full `ninja -C build check` | no behaviour change |
| `ninja -C build sani` | no leaks/UB in the new helpers |
| `tests/libtcc_test.c` | helper API round-trips |
| `pgo-bench` | no regression |

Add regression tests only for bugs found during migration.

## Acceptance criteria

- [ ] `CString`/`StringSlice` invariants are documented and asserted.
- [ ] High-traffic call sites use the struct APIs.
- [ ] Raw pointer/size pairs remain only at leaf/system boundaries.
- [ ] No test or benchmark regression.
- [ ] No new allocation failures or leaks under sanitizers.
