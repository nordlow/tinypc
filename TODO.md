<!-- -*- mode: markdown-ts; -*- -->

# TODO list

## Proposal: Signature Elision Rules (the "Rust" approach)

The goal is to avoid annotations in function bodies while still
working in TinyPC and across header files, using **convention-based
elision**.

Instead of inspecting the function body, the compiler infers the
qualifier mapping purely from the shape of the signature.

**The rule:**

> If a function returns a pointer and has exactly one pointer
> parameter, the return qualifier is automatically tied to that
> parameter.

Purity may also play a role here.

### Example

Standard C declarations:

```c
char *strchr(const char *s, int c);
void *memcpy(void *dest, const void *src, size_t n);
```

TPC would infer:

- `strchr`: has only one pointer parameter (`s`), so the return
  qualifier is inherited from `s`.
- `memcpy`: has several pointer parameters. The mapping is ambiguous,
  so it falls back to standard C behavior.

### Why this works well in TPC

- **Works across headers:** TPC only needs the function declaration
  from the `.h` file, not the body.
- **Easy to implement:** in `tccgen.c`, when parsing a function
  prototype, count the pointer parameters. If `pointer_arg_count == 1`
  and the return type is a pointer, set a single bit in the function's
  symbol entry: `VT_INFER_RETURN_QUAL`.  At call sites, if
  `VT_INFER_RETURN_QUAL` is set, copy the qualifier of that single
  argument to `vtop->type`.
- **Zero new keywords, zero sigils:** most utility functions
  (`strchr`, `trim`, `find`, `next_node`) take a single main input
  pointer and return a slice of it. They would "just work" with
  `immutable` and `const` for free.

For reference see
https://aistudio.google.com/prompts/191Hd2mWffHJ8B9OYybf6h2vFp9DH2LrK

## D-style `immutable` qualifier

`immutable` is accepted on instances and is represented by a distinct
`VT_IMMUTABLE` bit (see `TOK_IMMUTABLE` in `parse_btype()` and
`type_decl()`), while also implying `const`.  Immutability is
transitive:

- members of an immutable aggregate are immutable, including nested
  aggregates, array elements and pointer members, because member
  access propagates the qualifiers of the aggregate to the field;
- `indir()` propagates immutability from a pointer to the object it
  refers to, so writes through an immutable pointer are diagnosed too;
- `verify_assign_cast()` rejects converting a pointer to immutable
  data into a pointer to mutable data, while `immutable` -> `const`
  and mutable -> `immutable` conversions are accepted.

Remaining caveats:

- The qualifier follows C placement rules (`immutable int *p`
  qualifies the pointee, `int * immutable p` the pointer), unlike D's
  whole-type rule, and an explicit cast still discards immutability:

  ```c
  int *q = (int *)&immutable_obj;
  *q = 1; /* still accepted */
  ```

- Copying an immutable aggregate into a mutable one is allowed (value
  semantics), even when the aggregate holds pointers, so the copy may
  alias immutable data.
- Slice element types are not made immutable transitively, and
  `shared` is not modeled.  Full D semantics (no mutable alias exists
  anywhere) is a type-and-effect system that is not planned.

## Proposal: inout-style qualifier inference

D's `inout(T *) f(inout(T) *x)` is qualifier polymorphism.  TPC has no
generics and no AST, but the useful case is local to the call site:

- Store an `inout_arg` index in `struct FuncAttr` (the `f` bitfield of
  the function `Sym`), set by an attribute such as `@inout(1)` or
  `__attribute__((inout(1)))`.
- In the call handling in `unary()`, if `inout_arg` is set, qualify
  the result's pointee with the union of the declared qualifiers and
  the qualifier bits of argument N's pointee.  This is the "properties
  of the return infer the properties of `x`" rule, resolved statically
  per call.
- The argument side is already diagnosed: passing `const int *` where
  `int *` is expected triggers "assignment discards qualifiers" in
  `verify_assign_cast()`.
- The definition side is the hard half.  A pragmatic version checks
  only direct `return x;` statements syntactically; a full
  flow-sensitive check of the body is out of scope.

This complements the Signature Elision Rules proposal above: elision is
the zero-annotation fallback, the attribute is the explicit form.
Effort: about one day for plumbing plus one day for the syntactic
return check, much less after the attribute-table refactoring below.

## Proposal: Single-Pass Effect System with Source-Code Emission

Extend TPC's single-pass parser to infer function effects (purity,
immutability, mutation, allocation, error/exception throws) on the fly
and emit annotated declarations and definitions back into source files
or generated headers once inferred.

### How it Works in a Single-Pass Design

1. **On-the-Fly Effect Inference (`tccgen.c`):**
   - As expressions and statements inside a function body are parsed
     linearly, accumulate local effects into the function's `Sym` /
     `FuncAttr` bitmask (e.g., `EFFECT_PURE`, `EFFECT_READS`,
     `EFFECT_WRITES`, `EFFECT_ALLOC`).
   - For parameters and return types, combine signature elision and
     inout/immutability inference rules.

2. **Source/Header Emission Hook:**
   - At the closing brace `}` of a function definition, symbol resolution
     and type/effect inference are fully complete for that scope.
   - Using TPC's buffered source file pointers and line tracking
     (`BufferedFile`), output an updated declaration or definition
     containing the inferred effect annotations (e.g.,
     `__attribute__((pure))` or `@pure`, inferred `const` /
     `immutable` qualifiers) to a generated output stream
     (`-fwrite-effects`) or in-place file patch.

3. **Tradeoffs & Scope:**
   - **Advantage:** Preserves TPC's linear $O(N)$ single-pass speed while
     providing automated annotation of legacy C codebases without
     manual overhead.
   - **Limitation:** Forward references without prior declarations
     require two passes or deferred emission for functions called
     before definition; resolved by emitting declarations at
     end-of-file or via stub generation.

## Proposal: Compile-Time & Run-Time Reflection

Inspired by Zig and Jai, provide first-class compile-time and run-time
reflection for structs and pointers to enable zero-boilerplate
serialization and graph traversal.

### Semantics & Syntax
- **`$fields(T)` / `T.$fields`**: Evaluates at compile time to a
  tuple/array of field reflection nodes (containing `.name`, `.type`,
  `.offset`, `.size`, `.is_pointer`, etc.).
- **`$members(T)` / `T.$members`**: Evaluates to all members (fields,
  methods, nested types) of `T` as reference nodes. UFCS syntax
  (`T.$members`, `T.$hasAliasing`, etc.) greatly improves readability
  by chaining type inspections naturally.
- **Compile-Time Type Traits & Expressions**:
  - **Function/Trait style**: `$hasIndirections(T)`, `$hasAliasing(T)`,
    `$isA(T, U)`, etc. Functional syntax (e.g. `$isA(T, U)`) is
    particularly useful when composing higher-order functions
    operating on compile-time tuples and reflection nodes.
- **Alternative Syntaxes**:
  - `$is(T == struct)` / `$isA(T, struct)`
  - `T is struct` / `T $is struct` / `T $isA struct`
  - Enhanced readability when querying type categories (structs,
    pointers, scalars, qualifiers).
  - **Run-Time Type Information (RTTI)**: Emits static metadata tables
    (`struct RTTI`) into `.rodata` for runtime graph serialization,
    dynamic printing, and unmarshalling.
- **Compile-Time Control Flow (`$if`, `$for`, `$foreach`, `$else`,
  `$while`)**:
  - Replaces runtime loops and macros with compile-time control
    constructs prefixed with `$`.
  - Operates on reflection nodes (like `$fields(T)`) to conditionally
    include code and unroll loops statically at compile time.

### Use Case
- Zero-boilerplate binary tree serialization and complex graph
  traversal/relinking of pointer-linked nodes without manual
  serialization code.
# Bugs

- i386 fastcall is mostly wrong.
- `FPU st(0)` is left unclean (kwisatz haderach). Incompatible with
  optimized gcc/msc code.
- See transparent union problem in `/usr/include/sys/socket.h`.
- Precise behavior of `typeof` with arrays? (`__put_user` macro; should
  suffice for most cases.)
- Handle `? x, y : z` in unsized variable initialization (`,` is
  incorrectly considered a separator in the preparser).
- Transform functions to function pointers in function parameters
  (`net/ipv4/ip_output.c`).
- Fix function pointer type display.
- Check section alignment in C.
- Fix invalid cast in comparison `if (v == (int8_t)v)`.
- Finish `varargs.h` support (gcc 3.2 testsuite issue).
- Fix static functions declared inside a block.
- Fix multiple union initializations.
- Make libtcc fully reentrant (except for the compilation stage itself).
- Support struct/union/enum definitions in nested scopes (see also
  Debian bug #770657).
- `__STDC_IEC_559__`: `float f(void) { static float x = 0.0 / 0.0; return x; }`
- Memory may be leaked after errors (`longjmp`).

# Portability

- It is assumed that `int` is 32-bit and `sizeof(int) == 4`.
- `int` is used where host or target `size_t` would make more sense.
- TPC handles target floating-point (fp) values using the host's fp
  arithmetic. This is simple and fast, but may lead to exceptions,
  inaccuracy and wrong representations when cross-compiling.

## Linking

- Static linking (`-static`) works to some extent:
  - It works with musl libc.
  - glibc requires `libc.so` even when statically linked (very bad,
    but not up to tcc).

## Bound checking

- Fix bound exit on RedHat 7.3.
- `setjmp` is not supported properly in bound checking.
- Fix bound check code with `&` on local variables (currently done
  only for local arrays).
- Bound checking and float/long long/struct copy code; bound checking
  and symbol + offset optimization.

## Missing Features

- `TPC`: `--disable-asm` and `--disable-bcheck` options.
- `C / GCC`: `__builtin_expect()`.
- `C`: `atexit` (Nigel Horne).
- `C99`: Add complex types (gcc 3.2 testsuite issue).
- `C99`: Postfix compound literals (see `20010124-1.c`).
- `TPC`: Interactive mode / integrated debugger.
- `C / TPC`: `@attribute` syntax: `@name` / `@name(args)` for builtin
  and user-defined attributes, e.g. `struct T @nocopy {}`.
- `C, D`: **Attribute-Based Type/Storage Qualifiers (`@scoped`,
  `@static`, `@dynamic`, `@in`, `@out` `@const`, `@immutable`
  (`@immut`), `@mutable` (`@mut`), etc.)**: Allow storage and type
  qualifiers via `@`-prefixed attribute syntax (e.g., `@const int x`,
  `@static int y`). For variables and parameters.
- `D`: Track ownership of D slices (heap-allocated vs view of string
  literal) so that `a.length = n` can reallocate safely.
- `D`: Several array declarators with initializers (`int[] a = {1, 2,
  3}, b = {4, 5};`).
- `D`: **Built-in Associative Arrays (`V[K]`)**: Syntax and runtime
  support for hash maps, `.keys`, `.values`, `.remove()`, and the `in`
  operator.
- `D`: **Built-in UTF-8 `string` Type**: Dedicated UTF-8 `string` type
  that does *not* implicitly convert to `char[]`, with a property
  (e.g. `.bytes` or `.raw`) to access its raw underlying form as a
  slice of `ubyte`, `byte`, or `void`.
- `D`: **`with` Statement**: `with (expression) { ... }` or `with
  (symbol) { ... }` scope block opening member access without
  qualification.
- `D`: **Additional Array Properties**: `.ptr`, `.reverse`, `.init`,
  and `.stringof`.
- `D`: **Anonymous Functions & Lambdas**: Short lambdas `(x) => ...`
  and delegate literals.
- `D`: **`scope` Guards**: `scope(success)` and `scope(failure)`
  complementing `scope(exit)` (`defer`).
- `D, Zig, Jai`: **`typeof` and Metaprogramming**: `typeof(expr)`,
  `typeof(return)`, `typeid`, `__traits`, and reflection nodes
  (`$fields(T)`, `$members(T)`).
- `D`: **`ref` Function Parameters & Returns**: Reference parameters
  and returns without explicit pointer syntax.
- `D`: **`inout` Qualifier**: Qualifier polymorphism for functions
  preserving mutability.
- `D`: **Mixins**: String mixins (`mixin(...)`) for metaprogramming.
- `D`: **Manifest Constants**: Aggregate/struct manifest constants
  (`enum S = { ... };`) (scalar manifest constants are already
  covered).
- `D`: **`out` Function Parameters**: Parameters automatically
  initialized to default/zero upon entry.
- `D`: **`scope` RAII Objects**: `scope struct T obj = ...;` ensuring
  deterministic object destruction at scope exit.
- `D`: **`@safe`, `@trusted`, `@system` Attributes**: Function attributes
  enforcing memory safety.
- `D`: **`@nogc` Attributes**: Function enforcing non-GC-allocation.
- `Rust`: **`@unsafe` Attributes**: Function attributes
  allowing memory un-safety.
- `D`: **Property Call Syntax**: Omitting parentheses for 0-argument
  function calls (`obj.method`).
- `Zig`: **Saturating Arithmetic Operators**: Support saturating
  arithmetic operations (`+|`, `-|`, `*|`, `<<|` and assignment forms
  `+|=`, `-|=`, `*|=`, `<<|=`):
  - *Saturation Width*: Saturates at the usual-arithmetic-conversion
    type.
  - *Operator Set*: Limited to `+`, `-`, `*`, `<<` and their assignment
    forms.
  - *Shift Edge Cases (`<<|`)*: Defined as mathematical saturating
    shifts rather than undefined behavior.
  - *Operand Types*: Integers only; floats/pointers/complex rejected
    with standard error.
  - *Gating*: Always-on (since `+|` is never valid C).
  - *Constant Contexts & Preprocessor*: Folded at compile-time for
    enums, array sizes, `_Static_assert`, initializers, and `#if`
    expressions.
  - *Slice Op-Assign*: Integrates with slice op-assign gating.
  - *Evaluation Invariant*: Both operands are evaluated exactly once
    (using temporary variables).
- `Zig, Go, D`: **Slice `.len` Property Alias**: Support `.len` as an
  alias for `.length` on slices and arrays.
- `TPC`: Suppress specific anonymous symbol handling.
- `TPC`: More parse optimizations (faster compilation).
- `TPC`: Memory allocation optimizations.
- `TPC`: Optimize `VT_LOCAL` + const.
- `TPC`: Better local variables handling (needed for other targets).

## Architecture & Complexity Analysis

### High-Complexity Proposals
- **Single-Pass Effect System with Source-Code Emission**:
  - *Semantic Challenges*: Lacks an AST; expression structure is
    discarded immediately upon value pushing. Requires hooking
    `gfunc_call()`, `vstore()`, `indir()`, and assignments to
    reconstruct function summaries from codegen side effects.
  - *Interprocedurality*: Purity, throws, and allocation depend on
    callees, requiring a call graph and fixpoint analysis—conflicting
    with TPC's single-file separate compilation model. Function
    pointers, varargs, inline asm, and `setjmp`/`longjmp` complicate
    call graph construction.
  - *Aliasing & Emission*: Read/write effects require alias and escape
    analysis, interoperating with `immutable`/`inout`
    inference. Source emission faces macro expansion, `#line`
    directives, multi-file inputs, stdin, and system headers.
- **Full AST / IR**:
  - Highest architectural risk; invalidates TPC's core single-pass
    design philosophy that prioritizes speed and minimal memory
    footprint.
- **Split the Overloaded `Sym` Struct**:
  - Multi-subsystem refactor touching every part of the compiler (struct
    fields, variables, types, enum values, slice headers,
    temp-locals), carrying high regression risk.
- **Compile-Time & Run-Time Reflection (`$fields`, `$members`, traits, RTTI)**:
  - Equivalent to adding a metaprogramming subsystem (compile-time
    tuples, control flow constructs, traits, RTTI tables,
    serialization).
- **Formatting**:
  - TCC already has some support for printing values and types in
    diagnostics. Extend this to an API that returns lazy evaluated
    conversion from types and values to string and binary
    representations over various form such string, language itself,
    JSON, BSON, custom binary etc. API should look something like
    - @str(x), @s(x)
    - @fmt(x, ".3"), @f(x)
    where UFCS is supported aswell say as `x.@s`.
- **`inout` & Signature-Elision Inference**:
  - A compact surface area, but functions as a type-and-effect system,
    requiring strict semantic soundness per line of code.

## Proposal: Linear-Complexity ($O(N)$) Code Generation Improvements

Enhance TPC's single-pass performance and code quality while strictly
preserving linear time and space complexity:

- **Sliding-Window Peephole Optimizer**: Fixed-size window (last 2-3
  instructions) during emission to fold redundant moves,
  zero-extensions, or comparisons instantly.
- **SIMD-Accelerated Lexing**: Extend SIMD scans (`memchr`, vector
  comparisons) in `tccpp.c` for delimiter skipping and identifier
  scanning.
- **Bump-Pointer Arena Allocators**: Replace fine-grained
  `malloc`/`free` for symbols and token strings with cache-friendly
  arena allocation.
- **Enhanced Register Caching**: Refine register tracking on `vstack`
  across adjacent expression nodes to minimize spills and reloads.
- **Optimized Inline Helpers**: Tighter inline code sequences for
  frequent runtime operations like slice resizing and bounds checking.

# Refactorings for development speed
Ordered by (productivity gain) / (risk x effort):

- **Attribute table** (about one day, low risk).  `parse_attribute()`
  is a large switch over `TOK_*` pairs; every attribute needs two
  tokens, a case, and `AttributeDef`/`SymAttr` bits.  Replace it with
  a table of `{ name, flags, handler }` shared by
  `__attribute__((...))` and the pending `@name(...)` syntax.  This
  makes `immutable`, `inout`, `nocopy` and future attributes cheap.
- **Extract the type system into `tcctype.c`**.  Move `type_size`,
  `compare_types`, `is_compatible_*`, `verify_assign_cast`,
  `type_qualifiers`, `parse_btype_qualify` and `make_slice_type` out
  of the 10k-line `tccgen.c`, and add a small unit-test binary that
  links only that object.  Qualifier and type-inference work then
  becomes testable without compiling a whole program.
- **Delete the dead parser** (about half a day).  `#define
  precedence_parser` in `tccgen.c` compiles out the old
  recursive-descent expression parser; it is still there as a trap for
  anyone editing the wrong `expr_*` function.
- **Error recovery**.  `tcc_error()` longjmps and
  aborts on the first error.  Resynchronize at `;`/`}` and report
  several diagnostics per run.
- **Split `tccgen.c` into modules** (about one week, higher risk):
  `decl` / `expr` / `stmt` / `func` / `attrs`.  Do this after the
  attribute table and the type-system extraction.
- **Differential fuzzing**.  Compare TPC against GCC on generated
  snippets; the `tests/tests2` suite is already good but only covers
  hand-written cases.
- **Split the overloaded `Sym`** (large).  It doubles as struct field,
  variable, type, enum value, slice header and temp-local bookkeeping.
  Splitting it by kind is right long-term but is a multi-week,
  all-or-nothing change.
- **Pseudo backend for tests** (medium).  A bytecode or null backend to
  exercise parsing and typing without a target; useful for cross-target
  and type-system work.
- **Full AST/IR** (weeks, high risk, not planned near-term).  It would
  unlock optimizations and flow analyses, but would invalidate the
  single-pass design that makes TPC fast and small.  Prefer the debug
  dumps above instead.

# Not critical

- C99: fix multiple compound literal initializations in blocks (ISO
  C99 normative example; only relevant when using gotos). This
  requires a boolean variable telling whether the compound literal was
  already initialized.
- Add a PowerPC generator and improve codegen for RISC (need to
  suppress `VT_LOCAL` and use a base register instead).
- Fix preprocessor symbol redefinition.
- Add a portable bytecode generator and interpreter for other
  unsupported architectures.
- C++: variable declaration in `for`, minimal `class` support.
- win32: `__intxx`. Use resolve for bchecked `malloc` et al.
  Check exception code (exception filter function).
- Handle `void (__attribute__() *ptr)()`.
- VLAs are implemented in a way that is not compatible with signals:
  <http://lists.gnu.org/archive/html/tinycc-devel/2015-11/msg00018.html>

# Fixed (probably)

- Bug with defines:

  ```c
  #define spin_lock(lock) do { } while (0)
  #define wq_spin_lock spin_lock
  #define TEST() wq_spin_lock(a)
  ```

- `typedef`s can be structure fields.
- See `bugfixes.diff` + `improvement.diff` from Daniel Glockner.
- `long long` constant evaluation.
- Add `alloca()`.
- gcc `-E` option (since improved).
- `#include_next` support for `/usr/include/limits`?
- Function pointers/lvalues in `? :` (Linux kernel `net/core/dev.c`).
- win32: add `__stdcall`, check `GetModuleHandle` for DLLs.
- Macro substitution with nested definitions (ShangHongzhang).
- With `-run` and libtcc, a PLT is now built.
- Packed attribute is now supported.
- ARM and ARM64 code generators have been added.
