# Task: Peephole Optimizations

Add local instruction-pattern replacements to reduce redundant ops without
measurable compile-time overhead.

## Acceptance Criteria
- [ ] Patterns for redundant moves, no-ops, and simple arithmetic identities.
- [ ] No regression in `pgo-bench`.
- [ ] New regression tests under `tests/tests2/`.
