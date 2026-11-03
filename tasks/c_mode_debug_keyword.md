# Task: `__debug` Keyword and `@debug` in C Mode

> **Status:** Proposed feature specification (revised).

Make D-style conditional debugging blocks (`debug { ... }`, `debug(level) { ... }`,
`debug(identifier) { ... }`) and the `@debug` attribute available in **C mode**
(`-lang=c`) as well as D mode (`-lang=d`), without breaking existing C code that
uses `debug` as an ordinary identifier.

## Goal

Provide built-in conditional debugging blocks and attributes in C mode, eliminating
repetitive `#ifdef DEBUG` preprocessor boilerplate.

This is a **compiler extension**. Code using it will not compile with GCC, Clang, or
MSVC unless the portability fallback described below is used.

## Compatibility Principle

`debug` is a very common identifier in C (`int debug;`, `if (debug)`,
`struct { int debug; }`, `-Ddebug`). Therefore:

- The plain spelling `debug` **MUST NOT** become an unconditional keyword in C mode.
- The reserved spelling `__debug` is always available in C mode (identifiers
  containing a double underscore are reserved for the implementation, so no
  conforming program collides with it).
- The plain spelling `debug` is available in C mode **only on opt-in** (see below).
- D mode (`-lang=d`) behavior is unchanged: `debug` remains a keyword.

## Spellings

| Spelling             | D mode | C mode (default) | C mode (opt-in) |
|----------------------|:------:|:----------------:|:---------------:|
| `debug`              | yes    | identifier       | keyword         |
| `__debug`            | yes    | keyword          | keyword         |
| `@debug`             | yes    | yes              | yes             |
| `__attribute__((debug))` | n/a | yes (alias of `@debug`) | yes     |

**Opt-in for plain `debug` in C mode** (either mechanism, chosen per file or per build):

1. Compiler flag `-fdebug-keyword`.
2. Header `<tcc_debug.h>` containing `#define debug __debug`, mirroring how
   `<stdbool.h>` provides `bool` for `_Bool`. This keeps the opt-in explicit per
   translation unit and keeps the compiler core free of the collision problem.

`@` is not a valid token in standard C, so `@debug` cannot collide with existing
code. Lexer support for `@` is limited to recognizing `@debug` (and any other
`@`-attributes already supported in D mode); a bare `@` elsewhere remains an error.

## Enabling Debug Mode

Reuse the existing D-mode switches, extended to C mode:

| Flag               | Effect                                              |
|--------------------|-----------------------------------------------------|
| `-debug`           | Enable plain `debug { }` blocks.                    |
| `-debug=<level>`   | Enable `debug(n)` blocks where `n <= level`.        |
| `-debug=<ident>`   | Enable `debug(ident)` blocks matching `ident`.      |

- With none of these flags, all `debug` blocks and `@debug` items are disabled.
- These flags are independent of `-D` and `#define`. A macro named `DEBUG` has no
  effect on `debug` blocks, and vice versa.
- `debug(level)` and `debug(identifier)` take a decimal integer literal or a plain
  identifier. They are not macro-expanded and are not C expressions.

## Semantics & Syntax

Examples below use `__debug`; the same applies to `debug` when opted in.

1. **Conditional blocks**
   ```c
   __debug {
       printf("debug info\n");          /* compiled only when -debug is given */
   }

   __debug(2) { dump_state(); }         /* compiled when -debug=N, N >= 2 */
   __debug(trace) { trace_enter(); }    /* compiled when -debug=trace */
   ```

2. **`else` clause**
   ```c
   __debug { checked_path(); } else { fast_path(); }
   ```
   `else` is taken when the block is disabled. Standard dangling-`else` binding
   applies to the nearest `__debug`/`if`.

3. **`@debug` attribute**
   - On a function: the definition is compiled only when debugging is enabled.
     When disabled, the body is skipped and no symbol is emitted.
   - On a declaration or variable: same rule; when disabled, no declaration is
     introduced.
   - On a statement: equivalent to wrapping that single statement in `__debug { }`.
   - **Callers are the programmer's responsibility.** A call to or reference of a
     removed `@debug` function from non-debug code is an ordinary
     undeclared-identifier error. This is intentional and surfaces the mistake at
     compile time rather than at link time.

4. **Scope and declarations**
   - At **statement level** (inside a function), a `__debug { }` block is a
     compound statement and introduces a normal C block scope.
   - At **file scope**, declarations inside a `__debug { }` block are declared in
     the enclosing (file) scope and do not form a new scope, matching D's rule.
   - `@debug` never introduces a scope.

5. **Skipped-code rules**
   - Disabled blocks are **tokenized and brace/paren-balanced** but are **not
     type-checked and generate no code**. Undeclared identifiers inside a disabled
     block are not diagnosed.
   - Unlike `#if 0`, the skipped text must still be lexically valid (no unterminated
     strings or comments).
   - Preprocessing happens before this step, so macros inside a disabled block are
     expanded normally, and `#define`/`#include` directives inside it still take
     effect. Authors who need to suppress those should use `#if`.

6. **Ambiguity rule (opt-in `debug` only)**
   When plain `debug` is opted in, `debug` followed by `{` or by `( <level|ident> ) {`
   is a conditional block. Any other use of the token is a syntax error; the
   opt-in is explicit, so the compiler does not try to fall back to treating it as an
   identifier.

## Portable Fallback

For code that must also build with other compilers, provide a header pattern:

```c
#ifdef __TINYC__
#  include <tcc_debug.h>          /* debug -> __debug */
#else
#  ifdef DEBUG
#    define DEBUG_BLOCK(...) __VA_ARGS__
#  else
#    define DEBUG_BLOCK(...)
#  endif
#endif
```

Documentation should state plainly that `__debug` / `@debug` are not portable.

## Implementation Details

> Confirm file and function names against the current tree before starting; the
> split below is the intended design.

1. **Lexer / token layer (`tccpp.c`)**
   - [ ] Register `__debug` as a keyword in all language modes (remove the `lang_d`
     gate for this spelling only).
   - [ ] Register plain `debug` as a keyword in C mode only when `-fdebug-keyword`
     is set; leave D mode unchanged.
   - [ ] Recognize `@debug` in C mode (remove the `lang_d` gate on this form only).
   - [ ] Map `__attribute__((debug))` to the `@debug` token path.
   - [ ] Add `-fdebug-keyword`, and extend `-debug`, `-debug=<level>`,
     `-debug=<ident>` option parsing so they are accepted in C mode.

2. **Parsing and conditional skipping (`tccgen.c`)**
   - [ ] Reuse the existing `debug(...)` block parser and skipping machinery
     (`check_d_conditional()`) in C mode, rather than adding a second implementation.
   - [ ] Add `else` handling for debug blocks if not already present.
   - [ ] Apply the scope rules above (block scope at statement level; no new scope at
     file scope).
   - [ ] Ensure disabled blocks and `@debug` items emit no code and no symbols.

3. **Headers and docs**
   - [ ] Add `<tcc_debug.h>` (`#define debug __debug`).
   - [ ] Document the feature, flags, and non-portability in the user manual.

## Acceptance Criteria

- [ ] **P1:** `__debug { }`, `__debug(level) { }`, `__debug(ident) { }`, and `@debug`
  parse and evaluate correctly in C mode (`-lang=c`).
- [ ] **P1:** With no `-debug` flag, disabled blocks and `@debug` items emit no code
  and no symbols; with the flag, they are compiled.
- [ ] **P1:** Plain `debug` remains an ordinary identifier in C mode by default.
- [ ] **P2:** Plain `debug` works as a keyword with `-fdebug-keyword` or
  `<tcc_debug.h>`.
- [ ] **P2:** D mode behavior is unchanged.
- [ ] **Verification:** `./configure && make -j$(nproc) -B && make -j$(nproc) test`
  passes, including the new regression tests below.

## Regression Tests (`tests/tests2/`)

- [ ] `debug` used as variable, struct member, function name, and `-Ddebug` macro
  in default C mode (must still compile and behave).
- [ ] `__debug { }` enabled and disabled via `-debug`.
- [ ] Level form: `__debug(1)`, `__debug(3)` with `-debug=2`.
- [ ] Identifier form: matching and non-matching `-debug=<ident>`.
- [ ] `else` branch taken when disabled, skipped when enabled; dangling-`else` case.
- [ ] Nested `__debug` blocks (mixed enabled/disabled).
- [ ] File-scope vs function-scope declarations inside a block (scope rules).
- [ ] Disabled block containing undeclared identifiers (must not error) and an
  unterminated string (must error).
- [ ] `@debug` on a function, a variable, and a statement; enabled and disabled.
- [ ] Reference to a removed `@debug` function from non-debug code (expected
  error).
- [ ] Opt-in `debug` via `-fdebug-keyword` and via `<tcc_debug.h>`; malformed
  `debug` use under opt-in (expected syntax error).
- [ ] Bare `@` not followed by a supported attribute (expected error).
- [ ] D-mode (`-lang=d`) smoke test confirming unchanged behavior.

## Open Questions

- Should `-debug=<level>` and `-debug=<ident>` be combinable (e.g. repeated flags)?
- Should `@debug` support a parameterized form (`@debug(level)`) in C mode, matching
  `__debug(level)`?
- Is `else` on debug blocks already supported by the D-mode parser, or is it new
  work?
