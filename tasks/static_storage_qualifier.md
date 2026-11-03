# `@static` Qualifier and Inference for Static Storage Data

Investigate the feasibility, semantics, and utility of adding a `@static`
qualifier that is automatically inferred for static storage data (such as
string literals, global constants, and read-only data).

## Motivation

In systems programming languages like C and D, data residing in static storage
(such as string literals, file-scope variables, and constants in `.rodata`)
has a program-lifetime duration. Currently, string literals in C decay to
`char *` (or `const char *`), obscuring their provenance and static storage
lifetime. 

Introducing a `@static` qualifier (or type attribute) provides:
1. **Lifetime safety:** Ensuring references to stack or heap memory cannot
   accidentally alias or outlive static storage, or vice versa if restricted.
2. **Immutability and Purity integration:** Static storage data (like string
   literals) is inherently read-only; inferring `@static` aids `@pure` and
   `@det` functions by guaranteeing that accessed static data is immutable
   and deterministic.
3. **Optimization and Code Generation:** Allowing the compiler to place
   qualified data in read-only segments (`.rodata`) and optimize away
   unnecessary allocations or copies.

## Proposed Semantics

-  **Explicit Qualification:** A variable, parameter, or pointer type can be
  annotated with `@static` (e.g., `@static char *` or `char * @static`).
-  **Inference:** The compiler automatically infers `@static` for expressions
  and pointers pointing to static storage data, most notably string literals
  (`"hello"`), global `const` variables, and address-of expressions of
  file-scope static objects.
-  **Interoperability:** Seamless interoperation with standard C pointers,
  treating `@static` as a stricter provenance/lifetime qualifier (similar
  to `const` in terms of const-correctness propagation).

## Open Questions

1. **Storage-Class vs. Type Qualifier:** How does `@static` interact with
   C's existing `static` storage-class specifier (which denotes internal
   linkage or local static persistence)?
2. **Implicit Conversions:** Can a `@static T*` be implicitly converted to a
   plain `T*` or `const T*`? (Converting to `const T*` is safe; converting to
   mutable `T*` from read-only static data like string literals must be
   disallowed or emit a warning/error).
3. **Function Pointers and Globals:** Should global variables automatically
   acquire `@static` provenance?

## Acceptance Criteria

- [ ] Formal specification of `@static` qualifier grammar and scoping rules.
- [ ] Analysis of interaction with C type system (`const`, `volatile`) and
  existing attributes (`@pure`, `@det`).
- [ ] Prototype type-checking rules for string literal pointer assignments.
