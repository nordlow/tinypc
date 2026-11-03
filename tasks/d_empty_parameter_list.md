# Task: Allow `void f()` in place of `void f(void)` when `-lang=d`

> **Status:** Proposed (verify current behaviour first).

When `-lang=d`, treat an empty parameter list `()` as taking no
arguments (like C++ and D), rather than an unspecified K&R parameter
list.

## Goal

- `int f() { ... }` and `int f();` mean zero parameters in D mode.
- C mode keeps the standard C meaning: an old-style declaration with
  unspecified parameters.
- Calls with arguments to a D-mode `f()` are diagnosed.

## Investigate first

The note in the original task is important: parts of this may already
work by accident. Before changing anything:

1. Compile a small `-lang=d` program that calls `f(1)` where
   `int f() { return 0; }`, and check whether tcc already rejects it.
2. Check `gfunc_prolog()` / parameter-list parsing for
   `TOK_VOID`/empty-list handling.
3. Look for an existing `lang_d` guard around the old-style parameter
   path; the parser has a flag for "K&R parameter list being
   declared" near `tccgen.c:48`.
4. Record the result in this file before implementing.

## Implementation steps

1. In the parameter-list parser, when `-lang=d` and the list is
   empty, synthesise a prototype with zero parameters instead of a
   K&R list.
2. Ensure function *definitions* and *declarations* agree:
   - `int f();` then `int f() { ... }` must not warn about a mismatch.
3. Reject calls with arguments at the call site with a clear error,
   including the function name and the expected zero parameters.
4. Keep C behaviour unchanged; gate on `tcc_state->lang_d` only.
5. Keep function-pointer types consistent: `int (*p)()` in D mode
   takes zero arguments.
6. Update the D-mode section of `README.md`.

## Edge cases

- `int f(void);` must keep working in both modes.
- Variadic `int f(...)` and `int f(int, ...)`.
- Old-style definitions `int f(a) int a; { ... }` in C mode.
- Nested function declarations and function pointers.
- `typedef int F();` used with arguments in D mode.
- Empty parameter lists in prototypes used before the definition.

## Test plan

| file | asserts |
| --- | --- |
| `NN_d_empty_params_decl.c` | `int f();` then `f()` compiles |
| `NN_d_empty_params_call_error.c` | `f(1)` after `int f()` is a compile error |
| `NN_d_empty_params_void.c` | `int f(void)` still works |
| `NN_d_empty_params_ptr.c` | `int (*p)()` takes zero arguments |
| `NN_c_empty_params.c` | C mode keeps the K&R meaning |

Use the `-dt` mechanism for the expected-error test.

## Acceptance criteria

- [ ] D mode treats `()` as zero parameters in declarations and
      definitions.
- [ ] D mode rejects calls with arguments.
- [ ] C mode is unchanged.
- [ ] Regression tests under `tests/tests2/`.
