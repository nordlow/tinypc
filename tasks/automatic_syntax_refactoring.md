# Task: Automated In-Place Syntax Refactoring Pass (`tpc --refactor`)

> **Status:** Proposed. Tooling feature; does not affect compilation.

Implement an automated in-place refactoring pass in TinyPC (TPC) to
modernize legacy C constructs into preferred D-style idiomatic
constructs:

- C-style arrays (`int arr[N]`) -> D-style type-first arrays
  (`int[N] arr`).
- C-style indexing/counting `for` loops -> D-style `foreach` and
  `foreach_reverse`.

## Goal

- Provide safe, reversible, syntax-preserving rewrites for common
  migration patterns.
- Never modify a file that does not parse cleanly.
- Default to a dry run that prints a unified diff.

## Refactoring transformations

### 1. Type-first array syntax

- Match: `T var[N]` in variable definitions, function parameters,
  typedefs, and struct fields.
- Replace: `T[N] var`.
- Preserve:
  - array sizes (including expressions),
  - qualifiers (`const`, `immutable`, `volatile`, `@scoped`),
  - multi-dimensional shapes, applied in the correct order
    (`int a[2][3]` -> `int[2][3] a`),
  - comments attached to the declarator.
- Reject:
  - function declarators returning arrays,
  - arrays with attributes between `T` and the name if the rewrite
    would move the attribute,
  - K&R parameter declarations.

### 2. Loop modernization

- Match: monotone counting loops over a range or array:
  - `for (int i = 0; i < N; ++i)`
  - `for (int i = N - 1; i >= 0; --i)`
  - `for (i = 0; i < N; i++)` when `i` is declared outside.
- Replace:
  - `foreach (i; 0..N)`
  - `foreach_reverse (i; 0..N)`
- Preserve the loop body exactly; do not reorder side effects.
- Reject:
  - loops that mutate `i` in the body,
  - non-monotone steps (`i += 2`, `--i` with `<`),
  - floating-point counters,
  - loops with `break`/`continue` that depend on the post-loop value of
    `i` unless the refactor preserves it,
  - loops whose bound changes in the body.

### 3. CLI interface and safety

- `tpc --refactor=<rule> <file>`: one rule on one file.
- `tpc --refactor-all <dir>`: all safe rules on all `.c`/`.h`/`.d`
  files.
- `--refactor-dry-run`: print a git-style unified diff, do not write.
- `--refactor-in-place`: write the result atomically (temp file +
  rename).
- Refuse to run if the file has compiler errors.
- Refuse to run on files with `#if 0` regions that change the parse.
- Keep a `--refactor-keep-backup` option.

## Implementation steps

1. **Driver.** Add option parsing in `tcc.c` / `tcc_parse_args()`.
2. **Parse to tokens.** Use the existing preprocessor/lexer; do not
   build a new AST. Record token positions and whitespace.
3. **Pattern matcher.**
   - For arrays, work on declaration token runs in `decl()`.
   - For loops, match the `for` header token pattern.
4. **Rewrite engine.**
   - Splice token text while preserving comments and whitespace where
     possible.
   - Re-emit the file with original line endings.
5. **Validation.**
   - Re-parse the rewritten file; if it fails, abort and leave the
     original untouched.
   - Optionally compare object files before/after when the rule is
     semantics-preserving.
6. **Diff output.** Implement a small unified-diff writer; do not
   shell out to `git`.
7. **Tests.** Golden files: input `.c`, expected output `.c`, and a
   dry-run diff fixture.

## Edge cases

- Macros that expand to declarations: refactor the expansion or skip
  and report.
- `typedef` chains and `typeof`.
- Arrays of function pointers.
- Bitfields and nested structs.
- `for` loops inside macros.
- CRLF files and no-final-newline files.
- Idempotence: running the tool twice must not change the file again.

## Test plan

| test | asserts |
| --- | --- |
| `NN_refactor_array_simple` | `int a[10]` -> `int[10] a` |
| `NN_refactor_array_multi` | `int a[2][3]` order is preserved |
| `NN_refactor_loop_forward` | forward loop -> `foreach` |
| `NN_refactor_loop_reverse` | reverse loop -> `foreach_reverse` |
| `NN_refactor_skip_unsafe` | mutating loop body is left unchanged |
| `NN_refactor_idempotent` | second run is a no-op |
| `NN_refactor_dry_run` | diff matches golden output |
| `NN_refactor_parse_error` | a broken file is never modified |

Add these under `tests/` (tool tests), not `tests/tests2/`, unless the
harness can invoke the refactorer.

## Acceptance criteria

- [ ] Array declarations convert to D type-first syntax.
- [ ] Safe counting loops convert to `foreach`/`foreach_reverse`.
- [ ] Unsafe/ambiguous loops are left untouched with a report.
- [ ] Dry-run diff and atomic in-place write work.
- [ ] Rewritten output re-parses and passes the test suite.
- [ ] Tool tests are integrated into the build.
