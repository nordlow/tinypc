# Inferring Side Attributes and Side Effects of C Library Functions

> **Status:** Proposed investigation.

Investigate and establish automated or static/dynamic methods to infer
side attributes (e.g. purity, constness, safety, noexcept, memory
allocation/free behaviour) and side effects for standard C library
functions (`libc`).

## Goal

- Give the compiler trustworthy `@pure`/`@det`/`@safe`/allocation
  metadata for common libc functions so user code can call them from
  pure/safe contexts.
- Keep the metadata conservative: a wrong `@pure` is a
  miscompilation, a missing one is only a lost optimisation.
- Generate the table from a reproducible source, not from hand-edited
  guesses.

## Strategies

### 1. Copy D's definitions

Adopt or translate attribute annotations from D's standard library
(`object.d`, `std.stdc`) or D runtime knowledge for `printf`,
`memcpy`, `strlen`, `malloc`, and friends.

- Pros: curated, language-consistent.
- Cons: D's contract may not match the host libc exactly.

### 2. Traced execution (`strace` / `perf`)

Generate a C harness that invokes each function with representative
arguments under `strace`/`perf`, then record syscalls and memory/file
modifications.

- Pros: empirical.
- Cons: coverage gaps; a function may take an untraced branch.

### 3. Symbol/section analysis of `libc.so`

Inspect symbol tables, relocations, and instruction patterns to detect
I/O, memory mutation, and syscall interactions.

- Pros: no runtime harness.
- Cons: brittle across libc versions and architectures.

### 4. Static analysis and header annotation

Parse standard headers (`<stdio.h>`, `<string.h>`, `<stdlib.h>`) plus
escape/alias analysis to infer `const`/`pure`/allocation effects.

- Pros: source-level, portable.
- Cons: C aliasing makes "no writes" hard to prove.

## Proposed approach

Combine strategies, with static classification as the authority:

1. **Curated table.** Start from a reviewed table in
   `tools/libc-attrs.tsv` with columns:
   `name`, `pure`, `det`, `safe`, `allocates`, `frees`,
   `reads_global`, `writes_global`, `io`, `notes`, `source`.
2. **Generator.** `tools/gen-libc-attrs` turns the table into C
   declarations/registrations used by the compiler.
3. **Validation.** A test harness calls each function and checks the
   declared contract where observable (e.g. `strlen` must not write;
   `malloc` allocates; `printf` performs I/O).
4. **Escape hatch.** Users can override with
   `__attribute__((pure))`/`@system`; the compiler trusts explicit
   annotations over the table.

## Classification rules (initial)

| function | pure | det | safe | notes |
| --- | --- | --- | --- | --- |
| `strlen` | yes | yes | yes | reads only |
| `memcmp` | yes | yes | yes | reads only |
| `memcpy` | no | yes | yes | writes destination |
| `malloc` | no | no | no | allocates, address-dependent |
| `free` | no | yes | no | frees |
| `printf` | no | no | no | I/O |
| `sqrt` | yes | yes | yes | math, no errno if `-fno-math-errno` |
| `time` | no | no | yes | clock |

Mark anything involving `errno` as not `@det` unless the flag is
disabled.

## Implementation steps

1. Create `tools/libc-attrs.tsv` with a small initial set.
2. Write `tools/gen-libc-attrs` (shebang, executable) that emits a
   generated C header or table.
3. Wire the table into symbol lookup for libc names after header
   resolution; do not match by name alone when a user redefines the
   symbol.
4. Add a test that calls a sample of functions from `@pure` code and
   checks the compiler accepts/rejects the right ones.
5. Document provenance for every row in the `source` column.

## Tradeoffs

- **Completeness vs. correctness:** dynamic tracing may miss rare
  paths; static analysis can be over-conservative.
- **Portability:** glibc, musl, bionic, and MSVC CRT differ; keep
  platform-specific overrides in separate table fragments.
- **Maintenance:** regenerate and re-review when the host libc
  changes.

## Test plan

| file | asserts |
| --- | --- |
| `NN_libc_pure_ok.c` | `strlen`/`memcmp` callable from `@pure` |
| `NN_libc_impure_error.c` | `printf`/`malloc` rejected in `@pure` |
| `NN_libc_override.c` | explicit attribute overrides the table |
| `NN_libc_user_redef.c` | a user function with a libc name is not misclassified |

## Acceptance criteria

- [ ] Documented evaluation of inferred attributes for key libc
      functions.
- [ ] Generator under `tools/` with a reproducible table.
- [ ] Tests for inferred purity/safety attributes.
- [ ] Platform overrides documented.
