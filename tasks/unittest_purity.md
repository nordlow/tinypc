# Task: Stricter Unittest Purity Enforcement

## Goal

- [ ] **Optional/Default Unittest Purity:** Introduce compiler support
  (e.g., via `-funittest-pure` or `@pure unittest` syntax) to enforce
  strict purity and determinism guarantees on `unittest` blocks.
- [ ] **Side-Effect Auditing:** Reject or warn on global state mutation,
  non-deterministic calls (time, environment access), and unconstrained
  I/O inside pure unittests, while allowing assertions, local mutations,
  and designated logging/assertions.

## Motivation

- **Reproducibility:** Unittests are intended to test isolated,
  deterministic logic. Enforcing purity prevents tests from accidentally
  interfering with global state or depending on execution order.
- **CTFE Synergy:** Purity-checked unittests pave the way for evaluating
  pure test assertions at compile-time (compile-time unit testing / CTFE).

## Implementation Details (`tccgen.c`)

- **CLI Flags:** Add `-funittest-pure` (and `-fno-unittest-pure`) to
  enable global purity checking on all `unittest` blocks across a
  compilation unit.
- Extend `do_unittest()` to activate purity checks when `-funittest-pure`
  is active or when `@pure unittest` is explicitly specified.
- Route `unittest` block parsing through purity-checking scopes
  (`pending_block_pure`), reusing existing `@pure` enforcement logic
  (rejecting global variable writes, non-pure function calls).
- Provide an escape hatch (`@system unittest` or `@trusted unittest`)
  for tests requiring raw system interaction or I/O.
## Acceptance Criteria

- [ ] Unittest blocks under `-funittest-pure` correctly enforce purity
  constraints (rejecting global writes and impure calls).
- [ ] Explicit `@system unittest` allows tests with side effects.
- [ ] Verification: tests pass and new test cases verify purity rejection
  in unittests.
