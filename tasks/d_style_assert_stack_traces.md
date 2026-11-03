# Task: D-Style Descriptive Stack Traces Upon Failing Asserts

> **Status:** Proposed. Related: `pretty_printing_upon_failing_asserts.md`.

Add descriptive stack traces and function call backtraces upon failing
calls to `assert()`, mimicking D language runtime assertion failures
(`core.exception.AssertError` with stack trace).

## Goal

- Provide rich diagnostic backtraces showing file names, line numbers,
  and function names when an assertion fails.
- Avoid external heavy dependencies by reusing internal runtime
  debugging support where available.
- Keep the passing path zero-cost.

## Motivation

- Standard C `assert()` prints only file and line of the failed
  assertion itself, lacking caller context when assertions happen
  inside helper functions or deep call stacks.
- D language assert failures provide a complete call stack trace,
  drastically improving debuggability of invariant violations.

## Semantics & Output Format

When an assertion fails (e.g. `assert(x > 0)`), print:

1. The failure message, file name, and line number of the failing
   assertion.
2. A formatted stack backtrace listing active function calls, file
   names, and line numbers from the assertion site up to `main`.

Example output:

```
Assertion failure: x > 0 failed at main.c:25
--------------------------------------------------
[0] check_positive at main.c:25
[1] process_data at main.c:42
[2] main at main.c:55
```

## Fine-grained implementation strategy

1. **Compiler lowering & hook injection (`tccgen.c`):**
   - Modify assert handling (`do_d_assert` or equivalent in `tccgen.c`) to emit a call to a runtime assertion failure function (`__assert_fail_with_trace` or `_d_assert_fail`) instead of standard `__assert_fail`.
   - Pass source file name (string literal), line number (`int`), and failing expression string to the failure function.
2. **Runtime stack backtrace capture (`libtcc1` / runtime support):**
   - Implement `__capture_backtrace(void **buffer, int max_frames)` in `libtcc1.c` utilizing standard frame-pointer traversal (`ebp`/`rbp` chaining) or platform-specific backtrace APIs where available.
   - Guard against invalid frame pointers and stack bounds to prevent infinite loops or segmentation faults during unwinding.
3. **Symbol and line number resolution:**
   - Extend runtime symbol lookup or leverage embedded ELF symbol tables / DWARF line number info (if compiled with `-g`) to map instruction pointers (`rip`/`eip`) to function names, source file paths, and line numbers.
   - Provide a robust fallback mechanism that prints raw instruction addresses if debug info or symbol tables are absent.
4. **Formatting and output rendering:**
   - Implement a safe, stack-allocated formatting routine in `libtcc1.c` that avoids dynamic heap allocation (`malloc`/`free`) during assertion failure handling.
   - Output the formatted failure banner followed by numbered backtrace frames (`[0] func at file:line`).
## Edge cases

- Stripped binaries or missing debug symbols (gracefully fall back to
  raw instruction addresses).
- Signal safety and heap allocation during assertion failure (keep
  formatting robust and bounded).

## Test plan

| file | description |
| --- | --- |
| `NN_assert_stack_trace.c` | Verify stack trace includes caller frames |

## Acceptance criteria

- [ ] Failing `assert()` calls print a descriptive stack backtrace.
- [ ] Output includes function names, source files, and line numbers.
- [ ] Passing assertions incur zero performance overhead.
- [ ] Regression tests pass successfully.
