# Task: Refactor Stack-Based Value Operations (`vstack`/`vtop`)

> **Status:** Proposed. Behaviour-preserving refactor.

## Goal

Improve the maintainability, safety, and readability of low-level
`vstack` / `vtop` operations (such as `push_sym_value`,
`eval_sym_to_temp`, `vpush_string`, and related expression stack
manipulations in `tccgen.c`) without changing generated code.

## Context & Pain Points

TinyCC uses an explicit virtual expression stack (`vstack`, `vtop`) for
code generation. Functions like `eval_sym_to_temp`, `push_sym_value`,
`vpush_string`, `do_d_assert`, and related expression routines
frequently interleave low-level stack primitives:

- Push operations: `vpush()`, `vpush64()`, `vpushi()`, `vpushs()`,
  `vpushv()`, `vpushsym()`, `vpush_ref()`, `vpush_helper_func()`,
  `vpush_string()`.
- Pop, duplication, and setting: `vpop()`, `vdup()`, `vswap()`,
  `vset()`, `vsetc()`, `vseti()`.
- Stack reorganization: `vrott()`, `vrotb()`, `vrev()`.
- Evaluation, storage, and comparison flags: `vstore()`,
  `vcheck_cmp()`, `vset_VT_CMP()`, `vset_VT_JMP()`.
- Temporary variable management: `get_temp_local_var()`.

Problems:

- Manual `vtop++`/`vtop--` arithmetic is error-prone.
- Early returns can leave the stack unbalanced.
- Stack effects are implicit at call sites.
- Debugging a miscompile requires tracing each primitive.

## Proposed strategies

1. **Structured expression descriptors.** For complex features (D
   assert comparisons, slice operations), evaluate operands into
   explicit C structs first, then emit stack operations in one atomic
   step.
2. **Scoped stack guards.** Use block-scoped macros or a small
   `struct vstack_frame` with a saved `vtop` and a restore helper to
   keep the stack balanced on every exit path.
3. **High-level combinators.** Replace raw pointer math with helpers
   that manage boundaries and temporaries.
4. **Invariant assertions.** Add debug-only `vtop` delta checks before
   and after expression evaluation.

## Implementation phases

### Phase 0 — Baseline and guardrails

- Record `pgo-bench` Mlines/s and Valgrind `I refs`.
- Run `./configure --ninja && ninja -C build check`.
- Add a debug assertion helper (compiled out in release):
  `assert_vtop(expected)`.
- No behaviour change in this phase.

### Phase 1 — Scoped stack frame

- Add:
  ```c
  struct vstack_frame { int top; };
  static inline void vstack_frame_enter(struct vstack_frame *f) {
      f->top = vtop - vstack;
  }
  static inline void vstack_frame_leave(struct vstack_frame *f) {
      vtop = vstack + f->top;
  }
  ```
- Convert one function at a time (start with `do_d_assert`) to use the
  guard on every exit path.
- Verify identical generated code with `objdump` corpus comparison.

### Phase 2 — Replace manual arithmetic

- Audit `vtop++`, `vtop--`, and direct `vtop` writes in `tccgen.c`.
- Replace with `vpush*`/`vpop*` helpers where the intent is a push/pop.
- Keep direct writes only in the stack allocator itself.
- Add comments only where the operation is non-obvious.

### Phase 3 — Structured descriptors

- Introduce a small descriptor for the two-operand + operator pattern
  used by asserts and comparisons.
- Use it to remove repeated push/pop sequences.
- Do not add an AST; descriptors are stack-local C structs.

### Phase 4 — Invariant checks

- Enable `assert_vtop()` in debug builds.
- Run the full test suite with assertions on.
- Fix any imbalance uncovered, one commit each.

## Rules

- Every phase must keep generated code byte-identical on the test
  corpus unless a bug is fixed; record any intentional difference.
- Do not rename public/internal helpers in the same commit.
- Do not change the `SValue`/`CValue` layout.
- Keep changes confined to `tccgen.c` and small helpers in `tcc.h`.
- Run `ninja -C build sani` before committing.

## Test plan

| check | asserts |
| --- | --- |
| full `ninja -C build check` | no regressions |
| `ninja -C build sani` | no memory/UB errors |
| `pgo-bench` before/after | Mlines/s and `I refs` within noise |
| `objdump` corpus diff | identical code after refactor phases |
| debug assertion build | `vtop` balanced on the whole suite |

Add regression tests only for bugs uncovered by the assertions; the
refactor itself should not need new language-level tests.

## Acceptance criteria

- [ ] All manual `vtop` arithmetic outside the allocator is replaced
      or guarded.
- [ ] `assert_vtop()` passes on the full suite in debug builds.
- [ ] Generated code is unchanged on the corpus.
- [ ] No `pgo-bench` regression.
- [ ] Refactor is split into reviewable commits per phase.
