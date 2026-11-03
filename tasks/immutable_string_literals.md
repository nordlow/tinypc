# Task: Immutable C String Literals under D / Immutable Qualification

> **Status:** Implemented. Covered by `tests/tests2/386_d_immutable_string_literal.c`.

## Goal

Change the type of C string literals to `@immutable char*` (decaying
from an immutable character array `immutable char[N]`) when immutable
qualification is enabled (such as via `-lang=d`), ensuring the array
of data pointed to is transitively immutable.

## Background & Rationale

In standard C, string literals have type `char[N]` (decaying to
`char*`), which are modifiable in theory (though writing to them is
undefined behavior). With `-Wwrite-strings`, string literals receive
`const char*`.  In D and D-inspired dialects (`-lang=d`), string
literals represent constant text data that must be immutable
(`immutable(char)[]` or `@immutable char*`). Making string literals
`@immutable char*` prevents accidental or intentional modification of
string literal memory at compile time and aligns type semantics with D
specifications.

## Specification
1. **Detection**:
   -  Immutable qualification is enabled when `tcc_state->lang_d` is
     active (or when immutable qualification is explicitly enabled).
2. **Type Construction (`str_init` in `tccgen.c`)**:
   -  When immutable qualification is enabled, string literal element
     types (`TOK_STR`, `TOK_LSTR`, and `__FUNC__`) are qualified with
     both `VT_QUALIFIER_CONSTANT` and `VT_QUALIFIER_IMMUTABLE`.
   -  Resulting array type: `immutable char[N]` (or `immutable
     wchar_t[N]`).
   -  Decay type in expressions: `@immutable char*` (or `@immutable
     wchar_t*`).
3. **Semantics & Assignments**:
   -  Assigning or passing an `@immutable char*` / `immutable char[]` to a
     mutable pointer (`char*`) or mutable slice (`char[..]`) correctly
     triggers a compile-time diagnostic (`assignment discards
     'immutable' qualifier`), enforcing that immutable string literals
     cannot alias or corrupt mutable references.
   -  Array initialization (copying string literal data into a local
     mutable array buffer, e.g., `char buf[6] = "hello";`) remains
     valid via value/copy semantics.

## Verification
-  Add regression tests under `tests/tests2/`:
  -  `386_immutable_string_literal.c` / `.expect`: verify `@immutable
    char*` type and read operations.
  -  `387_immutable_string_literal_error.c` / `.expect`: verify
    compile-time errors when attempting to write to or discard
    immutable qualification from string literals.
