# Task: Require `@isolated` for Safe Mutable-to-Immutable Pointer Conversion

## Goal

Investigate and correct the rule regarding mutable-to-immutable pointer
conversions in TinyPC (`README.md:540`), replacing the flawed assumption that
any mutable pointer can implicitly decay to an immutable pointer with an
isolation-based requirement (`@isolated`).

## Problem Statement

Currently, `README.md` states:
> Converting a mutable pointer to an immutable one is allowed; dropping
> `immutable` requires an explicit cast.

This logic is flawed because aliased mutable pointers can be mutated through
another reference after being viewed as immutable, violating immutability
guarantees.

## Proposed Semantics & Isolation

1. **Isolation Rule (`@isolated`):**
   - Converting a mutable pointer to an immutable pointer (`T*` to
     `immutable T*` or `@immutable T*`) is **only** allowed if the source
     mutable pointer is verified to be `@isolated` (i.e., its underlying
     memory region is uniquely owned and transitively unreachable via any
     other active pointers).
2. **Free Constness Casting for Isolated Pointers:**
   - When a pointer or reference is proven `@isolated`, casting between
     constness / mutability (`mutable` $\leftrightarrow$ `immutable`) can be
     done freely without violating safety invariants.
3. **Documentation Update (`README.md`):**
   - Correct the documentation in `README.md` to reflect that implicit
     conversion to immutable requires `@isolated` proof.

## Acceptance Criteria

- **P1:** Document the flaw and isolation requirement in a design task.
- **P2:** Update type-checking logic (`tccgen.c`) to enforce `@isolated`
  checking on mutable-to-immutable pointer conversions.
- **P3:** Update `README.md` to accurately reflect the `@isolated` requirement.
- **Verification:** `./configure && make -j$(nproc) -B && make -j$(nproc) test`
  passes with regression tests in `tests/tests2/`.
