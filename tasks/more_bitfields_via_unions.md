# Task: Replace integer types with bitfields via union opt-ins

## Goal

Make logic dealing with raw integral bitmask flags in TinyPC (`int t`,
`unsigned short r`, etc.) more robust and type-safe by relying on
anonymous C11 unions that combine raw integers with structured bitfields,
vastly simplifying access syntax.

## Target Fields & Bitfield Layouts

1. **`CType.t` (`int t`):**
   - Currently packs basic types, qualifiers (`const`, `volatile`,
     `immutable`, `restrict`), storage classes, and signedness flags into a
     raw integer using bitmasks (e.g., `VT_BTYPE`, `VT_VAL_*`).
   - **Proposed Anonymous Union Representation:**
     ```c
     typedef struct CType {
         union {
             int t;
             struct {
                 unsigned int btype      : 4;  /**< Basic type (VT_VOID, VT_INT, etc.) */
                 unsigned int qualifiers : 6;  /**< Type qualifiers (const, volatile, etc.) */
                 unsigned int storage    : 5;  /**< Storage class (extern, static, etc.) */
                 unsigned int sign       : 2;  /**< Signed/unsigned flags */
                 unsigned int modifier   : 15; /**< Additional modifiers / attributes */
             };
         };
         struct Sym *ref;
     } CType;
     ```

2. **`SValue.r` & `SValue.r2` (`unsigned short r`, `unsigned short r2`):**
   - Packs register allocation indices and `VT_*` storage flags.
   - **Proposed Anonymous Union Representation:**
     ```c
     typedef struct SValue {
         CType type;
         union {
             unsigned short r;
             struct {
                 unsigned int reg_index  : 8;  /**< Physical/virtual register index */
                 unsigned int storage    : 8;  /**< VT_* storage / value location flags */
             };
         };
         union {
             unsigned short r2;
             struct {
                 unsigned int reg_index2 : 8;
                 unsigned int storage2   : 8;
             };
         };
         // ...
     } SValue;
     ```

## Benefits of Anonymous C11 Unions

- **Direct Syntax:** Anonymous unions allow accessing bitfields directly as
  `type.btype` or `vtop->r` without needing intermediate member names or
  `.raw` wrappers.
- **Zero Overhead & Full Compatibility:** Compiles to zero-cost bitmask/shift
  instructions while keeping the underlying integer fully accessible for raw
  bitwise operations where necessary.

## Acceptance Criteria

- **P1:** Define anonymous C11 union bitfield layouts for `CType.t`,
  `SValue.r`, and `SValue.r2` in `tcc.h`.
- **P2:** Update key parsing and code-generation logic in `tccgen.c` to use
  named bitfield accessors directly.
- **Verification:** `./configure && make -j$(nproc) -B && make -j$(nproc) test`
  passes with zero regressions.
