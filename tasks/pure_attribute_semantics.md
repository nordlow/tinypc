# Semantics for D-Like Function Attributes

> **Status:** Implemented. Covered by `tests/tests2/377`, `401`, `402`,
> `403`, and `404`.

Map to optimization semantics similar to GCC's `__attribute__((pure))`
or `__attribute__((const))`, depending on parameter types and memory
access guarantees.

For context, in GCC/Clang:

*  `__attribute__((pure))`: Can read global memory/dereference
  pointers, but produces no side effects.
*  `__attribute__((const))`: Stricter; cannot read global memory,
  return value depends solely on argument values.

In D, `pure` covers both "weakly pure" and "strongly pure". Clarifying
whether "depending" refers to D-style deduction or a future choice
between the two will help reviewers understand the intent.

-  Ensure test cases cover forward declarations (`int f(int x)
  @pure;`), not just definitions with bodies.

*  **Function pointers / types:** Does `@pure` attach to the function
  type or the declaration? (e.g., `int (*fn)(int) @pure`).

*  **Lexer impact:** If this is for C or C++, `@` is not standard
  syntax (outside Objective-C). Ensure there are tests verifying it
  doesn't conflict with Objective-C literals or macros if applicable.
