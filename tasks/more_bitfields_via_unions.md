# Task: Replace integer types with bitfields via union opt-ins

## Goal

Make logic dealing with raw integral bitmask flags in TinyPC (`int t`,
`unsigned short r`, etc.) more robust and type-safe via opt-in anonymous
unions combining raw integers with structured bitfields.

## Target Fields & Bitfield Layouts

1. **`CType.t` (`int t`):**
   - Currently packs basic types, qualifiers (`const`, `volatile`,
     `immutable`, `restrict`), storage classes, and signedness flags into a
     raw integer using bitmasks (e.g., `VT_BTYPE`, `VT_VAL_*`).
   - **Proposed Union Representation:**
     ```c
     typedef union {
         int raw;
         struct {
             unsigned int btype      : 4;  /**< Basic type (VT_VOID, VT_INT, etc.) */
             unsigned int qualifiers : 6;  /**< Type qualifiers (const, volatile, etc.) */
             unsigned int storage    : 5;  /**< Storage class (extern, static, etc.) */
             unsigned int sign       : 2;  /**< Signed/unsigned flags */
             unsigned int modifier   : 15; /**< Additional modifiers / attributes */
         };
     } CTypeBits;
     ```

2. **`SValue.r` & `SValue.r2` (`unsigned short r`, `unsigned short r2`):**
   - Packs register allocation indices and `VT_*` storage flags (e.g.,
     `VT_CONST`, `VT_LOCAL`, `VT_CMP`).
   - **Proposed Union Representation:**
     ```c
     typedef union {
         unsigned short raw;
         struct {
             unsigned int reg_index  : 8;  /**< Physical/virtual register index */
             unsigned int storage    : 8;  /**< VT_* storage / value location flags */
         };
     } SValueRegBits;
     ```

3. **`Sym.r` (`unsigned short r`):**
   - Symbol storage and register association flags.

## Design Pattern

Using anonymous unions in C11/GNU C:
```c
typedef struct CType {
    union {
        int t;
        struct {
            unsigned int btype      : 4;
            unsigned int qualifiers : 6;
            unsigned int storage    : 5;
            unsigned int sign       : 2;
            unsigned int modifier   : 15;
        };
    };
    struct Sym *ref;
} CType;
```

## Benefits

- **Readability & Safety:** Replaces error-prone manual bitwise shifts and
  masks (`(t & VT_BTYPE)`) with named bitfield accesses (`type.btype`).
- **Zero Overhead:** Compiles down to direct bitwise instructions without any
  runtime performance penalty.
- **Interoperability:** Preserves raw integer access (`type.t` or `type.raw`)
  for existing low-level checks and assignments.

## Acceptance Criteria

- **P1:** Define union bitfield wrappers for `CType.t`, `SValue.r`, and
  `SValue.r2`.
- **P2:** Update key parsing and code-generation logic in `tccgen.c` to use
  named bitfield accessors where appropriate.
- **Verification:** `./configure && make -j$(nproc) -B && make -j$(nproc) test`
  passes with zero regressions.
