# Specification: Scoped Declarations in Control Flow (`if`, `while`, `switch`, `do-while`)

> **Status:** Proposed. Extends control flow syntax to support local
> variable declarations and initializers scoped to the control
> structure, matching D language and C23/C++ standards.

## Goal

Enable scoped variable and type declarations with initializer clauses
directly within control flow statements:

```c
if (int x = compute(); x > 0) {
    use(x);
} else {
    handle_zero(x);
}

while (auto node = get_next(); node != NULL) {
    process(node);
}

switch (int val = get_code(); val) {
    case 1: ...
}
```

in D-mode and, where compatible (C23 init-statements and extensions),
in C-mode.

## Definitions & Scope

- **Init-statement / Declaration:** A statement consisting of one or
  more variable declarations with initializers, separated from the
  subsequent condition or expression by a semicolon (`;`).
- **Lifetime:** Variables declared in the init-statement are in scope
  throughout the condition, the controlled statement(s) (including
  `else` branches for `if`), and are destroyed or go out of scope when
  leaving the control construct.

## Syntax specification

1. **`if` with declaration:**
   - Syntax: `if (DECLARATIONS; CONDITION) STATEMENT [else STATEMENT]`
   - Example: `if (int p = parse(); p != 0) { ... }`

2. **`while` with declaration:**
   - Syntax: `while (DECLARATIONS; CONDITION) STATEMENT`
   - Example: `while (char* line = read_line(); line) { ... }`

3. **`switch` with declaration:**
   - Syntax: `switch (DECLARATIONS; EXPRESSION) STATEMENT`
   - Example: `switch (int err = check_status(); err) { ... }`

4. **`do-while` with declaration:**
   - Syntax: `do STATEMENT while (DECLARATIONS; CONDITION);` or
     `do { DECLARATIONS; STATEMENT } while (CONDITION);`
   - Example: `do { process(); } while (int more = check_more(); more);`

## Implementation steps

### 1. Parser extension in `tccgen.c`

- Update `parse_if`, `parse_while`, `parse_switch`, and `parse_dowhile`
  to check whether the token sequence inside the parentheses contains
  a declaration (e.g., following a type specifier or `auto`/`int`/etc.)
  followed by a semicolon `;` before the condition/expression.
- Open a new local scope (`new_scope_s`) before parsing the
  declarations.
- Parse the declaration(s) using existing declaration parsing routines
  (`decl0()` or local declaration parsers).
- Parse the condition expression in the same local scope.
- Ensure scope cleanup (`pop_scope()`) happens after the entire
  control structure (including `else` and loop bodies) finishes.

### 2. Mode enforcement

- Enable `if` and `switch` init-statements by default in C23 mode and
  D-mode (and as a language extension in older C standards with a
  diagnostic or warning if requested).
- Enable `while` and `do-while` init-statements in D-mode and as a
  supported extension in C-mode.

### 3. Diagnostics

- Diagnose use of uninitialized variables in init-statements.
- Diagnose name collisions with outer scope variables if shadowed
  (following language rules).

## Edge cases

- Multiple comma-separated declarations of the same type:
  `if (int x = 1, y = 2; x + y > 2)`
- Pointer and reference types (`auto*`, `const T&`, etc. in D/C++ extensions).
- Storage class specifiers (`const`, `static`, `register`, `auto`).
- Interaction with scope guards (`scope(exit)` in D).

## Test plan

| file | asserts |
| --- | --- |
| `NN_if_decl.c` / `.d` | `if (int x = f(); x)` scopes `x` to condition and bodies |
| `NN_while_decl.c` / `.d` | `while (int x = f(); x)` scopes `x` to loop iterations |
| `NN_switch_decl.c` / `.d` | `switch (int x = f(); x)` scopes `x` to switch block |
| `NN_dowhile_decl.c` / `.d` | `do-while` supports scoped init/condition |
| `NN_if_decl_scope.c` | variables go out of scope after statement ends |

## Acceptance criteria

- [ ] `if (DECLARATIONS; CONDITION)` parses and executes correctly in D and C-mode.
- [ ] `while (DECLARATIONS; CONDITION)` parses and executes correctly.
- [ ] `switch (DECLARATIONS; EXPRESSION)` parses and executes correctly.
- [ ] Variables are correctly scoped to the control construct.
- [ ] Comprehensive regression tests added under `tests/tests2/`.
