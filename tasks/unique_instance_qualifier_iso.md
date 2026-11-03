# Task: Reference Capabilities and `@unique` Instance Qualifier (Pony `iso`-Style)

Add support for reference capabilities and unique ownership semantics,
starting with a `@unique` instance qualifier inspired by Pony's `iso`
(isolated) reference capability.

## Motivation

In systems programming and safe concurrent languages (such as Pony and
Rust), managing object aliasing and mutation is critical for preventing
data races and undefined behavior without heavy locking overhead. 

Reference capabilities restrict how references can be aliased and shared:
- [ ] **Write-Isolation:** Ensuring an object has a single exclusive owner.
- [ ] **Move-Only Semantics:** Transferring ownership across scopes or tasks
  without allowing duplicate mutable aliases.

---

## Naming Discussion: `@unique` vs. `@isolated` vs. `@owned`

*Note on Naming:* We are uncertain whether `@unique`, `@isolated`, or
`@owned` is the best naming choice:
1. **`@unique`**: Emphasizes that there is only one reference pointing to
   the instance at any given time (strong uniqueness).
2. **`@isolated`**: Mirrors Pony's `iso` terminology, focusing on write-isolation
   and lack of interfering aliases.
3. **`@owned`**: Emphasizes exclusive ownership and move-only transfer semantics
   (similar to Rust's ownership model).

*Current Working Choice:* `@unique` is chosen as the primary keyword for
this task, but the design remains open to adopting `@isolated` or `@owned`
depending on community feedback and alignment with D/C language idioms.

---

## Proposed Semantics

1. **Exclusive Ownership & Uniqueness:**
   An instance declared or qualified with `@unique` guarantees that no
   other active mutable alias points to the same object graph.
2. **Move-Only Semantics:**
   Assigning or passing a `@unique` instance transfers ownership. Copying
   is implicitly disabled unless explicitly consumed or moved.
3. **Safe Aliasing Conversion:**
   A `@unique` reference can be safely promoted or downgraded to read-only
   or shared references under strict capability rules, but cannot be
   aliased mutably.

---

## Implementation Plan

1. **Attribute & Token Registration (`tcc.h`, `tcctok.h`):**
   - [ ] Register `@unique` (and optional aliases `@isolated` / `@owned`).
   - [ ] Add capability flag fields to `Sym`, `CType`, and `AttrDef`.
2. **Type Checking & Assignment Rules (`tccgen.c`):**
   - [ ] Enforce move semantics on assignment and function argument passing for
     `@unique` objects.
   - [ ] Disallow duplicating or re-using a moved `@unique` variable.
3. **Regression Tests:**
   - [ ] Add test cases under `tests/tests2/` verifying unique assignment,
     ownership transfer, and alias restriction errors.
