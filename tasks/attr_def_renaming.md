# Rename `AttributeDef` to `AttrDef`

Rename the struct and typedef `AttributeDef` to `AttrDef` across the
codebase to conform with other attribute naming conventions and keep
names concise (`SymAttr`, `FuncAttr`, `AttrDef`).

## Motivation

`AttributeDef` is currently used throughout `tcc.h`, `tccgen.c`, and
other compiler modules to represent parsed attribute sets (alignments,
storage, function attributes, etc.). Renaming it to `AttrDef` matches
the style of `SymAttr`, `FuncAttr`, and other concise type definitions
in the compiler.

## Implementation Plan

1. **Type Definition & Header Updates (`tcc.h`):**
   - Rename `typedef struct AttributeDef { ... } AttributeDef;` to
     `typedef struct AttrDef { ... } AttrDef;`.

2. **Codebase-Wide References (`tccgen.c`, `tccpp.c`, `tcc.c`, etc.):**
   - Update all function signatures, local variables, pointer
     parameters, and helper functions (such as `parse_btype`,
     `parse_attribute`, `merge_attr`, `sym_to_attr`, `patch_storage`,
     `external_sym`, `struct_layout`, etc.) taking or returning
     `AttributeDef *` to use `AttrDef *`.

3. **Documentation & Task Notes:**
   - Update any references in `TODO.md` and task documentation files
     where `AttributeDef` is mentioned.

## Acceptance Criteria

- All occurrences of `AttributeDef` renamed to `AttrDef`.
- Compiler builds successfully (`./configure && make -j$(nproc) -B`).
- All regression tests pass (`make -j$(nproc) test`).
