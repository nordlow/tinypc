# Task: Bounds Checking (-b)

Implement and fix robust bounds checking (`-b`) applicable across both
C and D codebases.

## Goal

- [ ] **setjmp Support:** Properly support `setjmp` and `longjmp` under
  bounds checking.
- [ ] **Address-of (`&`) Local Variables:** Extend bounds checking for
  `&` on local variables (currently restricted to local arrays).
- [ ] **Copy & Optimization Handling:** Fix bounds checking for float,
  long long, and struct copy operations, as well as symbol + offset
  optimizations.

## Implementation Details

- Audit `gbound` and runtime bounds checking hooks in `lib/libtcc1.c`
  and backend generators.
- Ensure pointer bounds table updates correctly during local variable
  address-taking and control flow jumps (`setjmp`).

## Acceptance Criteria

- All bounds-checking test suites pass under both C and D modes.
