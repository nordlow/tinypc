# Task: D-Style Descriptive Stack Traces Upon Null Pointer Dereferences

> **Status:** Proposed. Related: `d_style_assert_stack_traces.md`.

Add descriptive stack traces and function call backtraces upon null pointer
dereferences, mimicking D language runtime null pointer exception handling
(`core.exception.NullPointerError` with stack trace).

## Goal

- Intercept null pointer accesses (via signal handlers or runtime checks)
  and print a descriptive `NullPointerError` with file, line, and stack
  backtrace.
- Avoid uninformative raw core dumps (`SIGSEGV`) by providing immediate
  source-level context.
- Reuse stack unwinding and symbol resolution infrastructure from assert
  stack traces.

## Motivation

- Standard C programs crashing on null pointer dereference (`SIGSEGV`)
  produce cryptic hexadecimal instruction pointers or core dumps without
  immediate source location or call stack context unless run under a
  debugger.
- D language catches null pointer dereferences at runtime and prints a
  clean, human-readable call stack trace.

## Semantics & Output Format

When a null pointer dereference occurs (e.g. reading/writing through a null
pointer), print:

1. The null pointer error message and instruction address.
2. A formatted stack backtrace listing active function calls, file
   names, and line numbers from the fault site up to `main`.

Example output:

```
Null pointer dereference (SIGSEGV) at main.c:30
--------------------------------------------------
[0] deref_ptr at main.c:30
[1] process_data at main.c:48
[2] main at main.c:62
```

## Fine-grained implementation strategy

1. **Signal handler initialization (`libtcc1` startup / constructor):**
   - Register a `SIGSEGV` signal handler using `sigaction()` with `SA_SIGINFO` flag enabled during program initialization (or when runtime bounds/debugging options are active).
   - Save any pre-existing signal handler to chain or fallback if needed.
2. **Fault inspection & address validation (`siginfo_t` analysis):**
   - In the `SIGSEGV` handler, inspect `siginfo_t *info` to check if `info->si_signo == SIGSEGV` and examine `info->si_addr`.
   - Verify if `info->si_addr` falls within page 0 (null pointer page, e.g., `< 4096` or explicitly `NULL`). If it is a genuine null pointer dereference, proceed to generate the backtrace; otherwise, forward to the default handler or abort.
3. **Context extraction & backtrace unwinding:**
   - Extract the instruction pointer (`rip`/`eip`) and frame pointer (`rbp`/`ebp`) from the platform-specific `ucontext_t *` passed to the signal handler.
   - Invoke the shared stack backtrace capture and symbol resolution infrastructure (reused from assert stack traces).
4. **Descriptive error reporting and termination:**
   - Print `NullPointerError: Null pointer dereference at address ...` followed by the formatted function call backtrace (`[0] func at file:line`).
   - Terminate execution cleanly via `_exit()` or `abort()`, bypassing standard signal crashes that lack source context.
## Edge cases

- Non-null segmentation faults (e.g., stack overflow or invalid memory
  protection): fall back to standard core dump behavior or generic signal
  handling.
- Stripped binaries or missing debug symbols (fall back to raw instruction
  addresses).

## Test plan

| file | description |
| --- | --- |
| `NN_null_pointer_stack_trace.c` | Verify null pointer dereference prints stack trace |

## Acceptance criteria

- [ ] Null pointer dereferences trigger a descriptive stack backtrace.
- [ ] Output includes function names, source files, and line numbers.
- [ ] Non-null segmentation faults are distinguished and handled appropriately.
- [ ] Regression tests pass successfully.
