# Task: Support Nested Functions (GNU C Extension)

## Goal
Implement support for GNU C nested functions (declaring functions
inside compound statement blocks and accessing enclosing lexical scope
variables via static chains and trampolines).

## Affected Components & Code Details

### 1. Parser & Scope (`tccgen.c`)
-  **Error Removal**: Remove or conditionalize the check at
  `tccgen.c:11205` that currently emits `error: cannot use local
  functions`.
-  **Block-level Function Definitions**: Update `block()` / `decl()`
  parsing to permit function declarations and definitions inside
  compound statements.
-  **Symbol Management**:
  -  Push nested function symbols onto `local_stack` via `sym_push()` so
    they respect lexical scope boundaries (`local_scope`).
  -  Handle forward declarations and scope cleanup in `sym_pop()`.

### 2. Lexical Scoping & Upvalues (Static Chain)
- [ ] **Static Chain Register**: Reserve and assign a dedicated
  architecture-specific register to act as the static chain pointer
  (e.g., `%r10` on x86_64, corresponding to GNU C's nested function
  ABI).
-  **Variable Resolution**:
  -  Extend identifier resolution in `tccgen.c` to detect when a
    reference accesses a local variable belonging to an outer
    enclosing function.
  -  Generate code to load variable addresses relative to the static
    chain pointer instead of the current frame pointer
    (`FP`/`%rsp`/`%rbp`).

### 3. Trampolines (`*-gen.c` and `libtcc`)
-  **Address-of Nested Functions (`&nested_fn`)**: When taking the
  address of a nested function, a standard code pointer is
  insufficient because it lacks the enclosing environment.
-  **Trampoline Code Generation**:
  - [ ] Implement target-specific trampoline routines in code generators
    (`x86_64-gen.c`, `i386-gen.c`, `arm-gen.c`, `arm64-gen.c`,
    `riscv64-gen.c`).
  -  Emit an executable stub (trampoline) on the stack/heap that:
    1. Loads the static chain pointer into the static chain register.
    2. Jumps to the actual entry point of the nested function.
  -  Mark trampoline memory pages executable (similar to GNU C / libgcc
    `__trampoline_setup`).

### 4. Function Calls (`gfunc_call`)
-  Update `gfunc_call()` and indirect call generation across
  architecture backends (`x86_64-gen.c`, `i386-gen.c`, etc.) to pass
  the static chain pointer in the designated static chain register
  when invoking a nested function or trampoline.
