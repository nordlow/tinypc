# Inferring Side Attributes and Side Effects of C Library Functions

Investigate and establish automated or static/dynamic methods to infer
side attributes (e.g. purity, constness, safety, noexcept, memory
allocation/free behavior) and side effects for standard C library
functions (`libc`).

## Strategies

-  **Copy D's definitions:**
  Adopt or translate attribute annotations from D's standard library
  (`object.d`, `std.stdc`) or D runtime knowledge regarding standard
  C functions (`printf`, `memcpy`, `strlen`, `malloc`, etc.).
-  **Traced Execution (`strace` / `perf`):**
  Generate a comprehensive test harness in C that invokes standard
  library functions under `strace`, profiling or recording actual
  system calls made and memory/file modifications per function.
-  **Symbol / Section Analysis of `libc.so`:**
  Inspect symbol tables, relocation entries, and instruction patterns
  in shared C library binaries (`libc.so`) to detect I/O, memory
  mutation, and system call interactions.
- [ ] **Static Analysis & Header Annotation:**
  Parse standard headers (`<stdio.h>`, `<string.h>`, `<stdlib.h>`)
  combined with static escape analysis, pointer aliasing checks, and
  attribute inference rules (e.g., detecting absence of pointer
  dereferences or global writes to infer `const` / `pure`).

## Tradeoffs

-  **Completeness vs. Correctness:** Dynamic tracing may miss rare
  code paths or platform-specific syscalls. Static analysis can be
  overly conservative due to pointer aliasing in C.
- [ ] **Portability:** C library implementations vary across platforms
  (glibc, musl, bionic, MSVC CRT), making hardcoded tables brittle.

## Acceptance Criteria

- [ ] Documented evaluation of inferred attributes for key `libc` functions.
- [ ] Automated mechanism or generator script placed under `tools/`.
- [ ] Test cases covering inferred purity/safety attributes.
