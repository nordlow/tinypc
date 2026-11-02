# Task: C3-Style Free Self-Functions (`self` First Parameter & UFCS)

> **Status:** Proposed feature specification.

Investigate and design support for C3-style free self-functions in TinyPC (TPC),
where functions taking a `struct` (or struct pointer) as their first
parameter (`self`) can be invoked using dot-syntax (`obj.method(args)`), and
investigate making this pattern the mandatory mechanism for defining struct
methods (unifying free functions and object methods ala C3 and D UFCS).

## Goal

Allow free functions taking a struct instance as their first parameter to act
as methods on that struct, and optionally mandate that all struct methods be
expressed as free self-functions.

## Semantics & Syntax

1. **Free Self-Function Definition:**
   - Functions take the struct instance as their first parameter (conventionally
     named `self` or similar):
     ```c
     void Point_print(Point* self, int offset) {
         printf("%d\n", self->x + offset);
     }
     ```
2. **Dot-Syntax Invocation (UFCS Integration):**
   - When calling `pt.Point_print(10)` or `pt.print(10)` (if named/aliased),
     the compiler automatically rewrites it to `Point_print(&pt, 10)` or
     `Point_print(pt, 10)` based on parameter passing semantics (by pointer vs
     by value).
3. **Mandatory Method Style:**
   - Investigate enforcing that struct methods cannot be declared inside the
     struct body but must instead be defined as free self-functions, keeping
     struct layouts purely data-focused and decoupling data representation
     from behavior.

### Relationship with Existing UFCS

> **Conclusion:** C3-style "self" functions and general UFCS are identical in
> underlying mechanism. Naming the first parameter `self` is purely an
> idiomatic naming convention; the compiler dispatches based on the **type**
> of the first parameter matching the left-hand-side receiver. Therefore,
> TinyPC's existing UFCS implementation already covers C3-style free self-functions
> conceptually, requiring no separate language feature beyond standard UFCS
> rules.

## Implementation Details (`tccgen.c`)

1. **Method Resolution & UFCS Extension (`tccgen.c`):**
   - Extend member call resolution (`.`) to check if the target function is
     a free function taking the left-hand-side expression's type as its first
     parameter.
2. **Automatic Address-Of / Dereference Adjustment:**
   - Automatically take the address of lvalues (`&obj`) when the free
     function expects a pointer (`Struct* self`), or pass by value when it
     expects `Struct self`.

## Acceptance Criteria

- **P1:** Support C3-style free self-functions called via dot syntax
  (`obj.func(args)`).
- **P2:** Investigate and specify mandatory free-function method separation for
  structs.
- **Verification:** `./configure && make -j$(nproc) -B && make -j$(nproc) test`
  passes with new regression tests under `tests/tests2/`.
