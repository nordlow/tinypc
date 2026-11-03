# Task: Replace integer types with bitfields via union opt-ins

## Goal

Make logic dealing with raw integral bitmask flags in TinyPC (`int t`,
`unsigned short r`, etc.) more robust and type-safe by relying on
anonymous C11 unions that combine raw integers with structured bitfields,
vastly simplifying access syntax without sacrificing zero-overhead
performance or binary compatibility.

## Target Fields & Bitfield Layouts

### 1. `CType.t` (`int t`)
- [ ] **Current usage:** Packs basic types, qualifiers (`const`, `volatile`,
  `immutable`, `restrict`), storage classes, and signedness flags into a
  raw integer using bitmasks (e.g., `VT_BTYPE`, `VT_VAL_*`).
- [ ] **Proposed Anonymous Union Representation:**
  ```c
  typedef struct CType {
      union {
          int t;
          struct {
              unsigned int btype      : 4; /**< Basic type (VT_VOID, VT_INT, etc.) */
              unsigned int qualifiers : 6; /**< Type qualifiers (const, volatile, etc.) */
              unsigned int storage    : 5; /**< Storage class (extern, static, etc.) */
              unsigned int sign       : 2; /**< Signed/unsigned flags */
              unsigned int modifier   : 15; /**< Additional modifiers / attributes */
          };
      };
      struct Sym *ref;
  } CType;
  ```

### 2. `SValue.r` & `SValue.r2` (`unsigned short r`, `unsigned short r2`)
- [ ] **Current usage:** Packs register allocation indices and `VT_*` storage flags.
- [ ] **Proposed Anonymous Union Representation:**
  ```c
  typedef struct SValue {
      CType type; /**< Complete C type descriptor */
      union {
          unsigned short r;
          struct {
              unsigned int reg_index  : 8; /**< Physical/virtual register index */
              unsigned int storage    : 8; /**< VT_* storage / value location flags */
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

### 3. `Sym.r` (`unsigned short r`)
- [ ] **Current usage:** Associated register, or `VT_CONST`/`VT_LOCAL` storage
  class and lvalue flags for symbols in the symbol table.
- [ ] **Proposed Anonymous Union Representation:**
  ```c
  typedef struct Sym {
      int v; /**< Symbol token identifier */
      union {
          unsigned short r;
          struct {
              unsigned int reg_index  : 8; /**< Register allocation index */
              unsigned int storage    : 8; /**< Storage class / lvalue flags */
          };
      };
      struct SymAttr a; /**< Extended symbol attributes */
      // ...
  } Sym;
  ```

### 4. Other Packed Integer Fields in Compiler Structs
- [ ] **`ExprValue.pcrel`:** Can be integrated into a union combining raw flags
  with structured relocation flags.
- [ ] **`BufferedFile` flags & line states:** Encapsulating raw integer state
  flags into bitfield-backed unions for cleaner source tracking.

## Benefits of Anonymous C11 Unions

- [ ] **Direct Syntax:** Anonymous unions allow accessing bitfields directly as
  `type.btype`, `sval.reg_index`, or `sym.reg_index` without needing
  intermediate member names or `.raw` wrappers.
- [ ] **Zero Overhead & Full Compatibility:** Compiles to zero-cost bitmask/shift
  instructions while keeping the underlying integer fully accessible for raw
  bitwise operations where necessary.

## Acceptance Criteria

- [ ] **P1:** Define anonymous C11 union bitfield layouts for `CType.t`,
  `SValue.r`, `SValue.r2`, and `Sym.r` in `tcc.h`.
- [ ] **P2:** Update key parsing and code-generation logic in `tccgen.c` to use
  named bitfield accessors directly where appropriate.
- [ ] **Verification:** `./configure && make -j$(nproc) -B && make -j$(nproc) test`
  passes with zero regressions.
