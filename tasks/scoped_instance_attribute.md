# `@scoped` Support on Instances (Transitive by Default, Immutable Exemption)

Add `@scoped` support on instance declarations, drawing inspiration from
Pony's transitive reference guarantees rather than D's weaker,
non-transitive `scope` semantics.

## Design Decision: Transitivity & Immutable Exemption

**Should `@scoped` be transitive? Yes.**
**Are `@immutable` qualified fields exempt? Yes.**

1. **Transitive Lifetime Bounds:** If an instance `x` is marked
   `@scoped`, every mutable pointer, reference, or aggregate field
   transitively reachable from `x` inherits the scoped lifetime
   constraint.
2. **Leak Prevention for Mutables:** Extracting or assigning a mutable
   pointer/reference field from a `@scoped` instance to any destination
   with a broader lifetime (global variables, heap allocations, or outer
   scopes) is a compile-time error.
3. **Immutable Exemption:** If a field or data structure transitively
   reachable from a `@scoped` instance is qualified as `@immutable`
   (or read-only/constant), it **is allowed to leak** or escape the
   enclosing scope. Because immutable data cannot be modified, sharing
   or escaping immutable references introduces no risk of dangling
   mutable aliases or data races.

## Goal

- Accept `@scoped` as a variable attribute/modifier (`@scoped T x;` or
  `T x @scoped;`) in D language mode (`-lang=d`).
- Enforce **transitive** escape analysis at compile time on mutable
  references, while permitting `@immutable` references to escape safely.
- Issue precise diagnostic errors on scope violations.

## Surface Syntax

Gated on `-lang=d` mode:

```c
struct Node {
    int *mut_ptr;
    @immutable char *imm_name;
};

void foo() {
    int local_val = 42;
    @scoped struct Node n;
    n.mut_ptr = &local_val;
    n.imm_name = "shared_string";

    // global_ptr = n.mut_ptr;   /* Error: transitive escape of mutable scoped reference */
    global_imm = n.imm_name;     /* OK: @immutable fields are allowed to leak */
}
```

## Implementation Plan

### Phase 1: Lexer & Attribute Definitions

1. **Token definition (`tcctok.h`):**
   - Register `TOK_scoped` and `TOK_immutable` or handle as D attribute
     keywords.
2. **Attribute structure (`tcc.h`):**
   - Add `int scoped;` and `int imm;` flags to `AttrDef`, `Sym`,
     and `CType` structs.

### Phase 2: Parsing & Propagation

1. **`parse_d_attributes()` (`tccgen.c`):**
   - Recognize `@scoped` and `@immutable` in `parse_d_attributes()`
     and set respective flags.
2. **Transitive Type Qualification (`CType`):**
   - Propagate scoped properties transitively across fields, skipping
     any field or subtype marked `@immutable`.

### Phase 3: Transitive Escape Analysis & Checks

1. **Assignment & Field Extraction Checks (`gassign()` & member access):**
   - When accessing fields of a `@scoped` instance (`n.mut_ptr`), check if
     the field type is `@immutable`.
   - If `@immutable`, permit assignment to broader scopes.
   - If mutable, disallow assigning or copying to wider lifetimes
     (globals, statics, outer scopes) and emit compile error:
     `"transitive escape of mutable @scoped reference '%s'"` via
     `tcc_error_noabort()`.
2. **Return Statement Checks (`parse_return()` in `tccgen.c`):**
   - Disallow returning mutable scoped references, while permitting
   - `@immutable` references/strings to be returned safely.

## Acceptance Criteria

- Transitive `@scoped` attribute enforcement on mutable instance fields.
- Exemption allowing `@immutable` fields to escape scopes safely.
- Regression test suite under `tests/tests2/`.
