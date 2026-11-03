# Task: Leaf Function Prologue/Epilogue Trimming

Trim unnecessary frame setup for leaf functions that don't need it.

## Acceptance Criteria
- [ ] Detect leaf functions (no calls, no stack spills).
- [ ] Omit frame pointer setup where safe.
- [ ] No regression in tests or `pgo-bench`.
