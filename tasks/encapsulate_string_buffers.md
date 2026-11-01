# Encapsulate Bounded String Buffers into Structs (`StrBuf` / Slices)

> **Status:** Proposed refactoring task.

Refactor raw `(char *buf, size_t buf_size)` passing across compiler utility
functions (`pstrcpy`, `pstrcat`, `pstrncpy`) into a safe, ergonomic bounded
string/buffer struct (fat pointer / buffer struct tracking pointer, capacity,
and length).

## Motivation

Currently, TinyPC codebase relies on raw pointer and size pairs
(`char *buf, size_t buf_size`) for string operations. This leads to:
- Verbose and repetitive parameter passing at every call site.
- Potential bugs if `buf_size` is miscalculated or mismatched.
- Lack of high-level abstractions for string building and concatenation.

## Proposed Design

1. **Buffer Struct (`StrBuf` or similar):**
   ```c
   typedef struct {
       char   *ptr;
       size_t  capacity;
       size_t  length;
   } StrBuf;
   ```
2. **Method-like API:**
   - `strbuf_init(&buf, storage, sizeof(storage))`
   - `strbuf_copy(&buf, src)`
   - `strbuf_cat(&buf, src)`
3. **Integration & Ergonomics:**
   - Replace manual `pstrcpy`/`pstrcat` calls with cleaner buffer struct
     operations.
   - Align with D-style slice and buffer semantics where applicable.

## Acceptance Criteria

- **P1:** Define `StrBuf` and safe wrapper functions replacing raw
  `pstrcpy`/`pstrcat`/`pstrncpy` patterns.
- **P2:** Refactor high-traffic call sites across `libtcc.c`, `tccgen.c`,
  and `tccpp.c` to use the new buffer struct.
- **Verification:** `./configure && make -j$(nproc) -B && make -j$(nproc) test`
  passes with zero regressions.
