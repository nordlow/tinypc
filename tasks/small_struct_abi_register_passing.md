# Task: Small Struct ABI Register Passing

Pass small structs/unions in registers per ABI instead of on stack.

## Acceptance Criteria
- [ ] Correct handling for x86-64 and ARM64.
- [ ] No ABI regressions in `tests/`.
- [ ] New ABI tests under `tests/tests2/`.
