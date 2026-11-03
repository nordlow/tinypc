# Task: Dead/Unreachable Block Elimination

Remove unreachable basic blocks during code generation to shrink output and
improve runtime performance.

## Acceptance Criteria
- [ ] Eliminate blocks after unconditional jumps/returns.
- [ ] Preserve debug info correctness.
- [ ] No regression in `pgo-bench`.
