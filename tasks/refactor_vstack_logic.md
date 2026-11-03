# Task: Refactor Stack-Based Value Operations (`vstack`/`vtop`)

## Goal Improve the maintainability, safety, and readability of

low-level `vstack` / `vtop` operations (such as `push_sym_value`,
`eval_sym_to_temp`, `vpush_string`, and related expression stack
manipulations in `tccgen.c`).

## Context & Pain Points

TinyCC uses an explicit virtual expression stack (`vstack`, `vtop`)
for code generation. Functions like `eval_sym_to_temp`,
`push_sym_value`, `vpush_string`, `do_d_assert`, and related
expression routines frequently interleave low-level stack primitives:

- [ ] Push operations: `vpush()`, `vpush64()`, `vpushi()`, `vpushs()`,
  `vpushv()`, `vpushsym()`, `vpush_ref()`, `vpush_helper_func()`,
  `vpush_string()`.
- [ ] Pop, duplication, and setting: `vpop()`, `vdup()`, `vswap()`,
  `vset()`, `vsetc()`, `vseti()`.
- [ ] Stack reorganization: `vrott()`, `vrotb()`, `vrev()`.
- [ ] Evaluation, storage, and comparison flags: `vstore()`,
  `vcheck_cmp()`, `vset_VT_CMP()`, `vset_VT_JMP()`.
- [ ] Temporary variable management: `get_temp_local_var()`.

## Proposed Strategies for Maintainability

1. **Structured Expression Descriptors**: For complex language
   features (like D assert comparisons or slice operations), evaluate
   operands into explicit C structs/descriptors first, then emit the
   stack operations in a single atomic step rather than piecemeal
   stack pushes.
2. **GCC `__attribute__((cleanup))` & Scoped Stack Guards**: In C, use
   cleanup functions and block-scoped macros to automatically balance
   `vtop` or clean up temporary stack references when leaving a local
   scope, preventing stack leaks on early error returns.
3. **High-Level Combinators & Functional Builders**: Replace raw
   imperative pointer math (`vtop++`, `vtop--`, `vrott`) with helper
   combinators that manage stack boundaries and temporary local
   allocations safely within block scopes.
4. **Invariant Assertions & Stack Depth Tracking**: Add explicit stack
   depth checks and debug assertions (e.g., asserting expected `vtop`
   delta before and after expression evaluation) to catch stack
   imbalance at compile time.
