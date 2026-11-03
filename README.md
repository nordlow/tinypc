# Tiny Poly Compiler (TPC)

**TinyPC** (TPC for short) is a small, fast, self-hosting compiler for
C and a growing D-inspired dialect.  It is derived from TinyCC, the
*Tiny C Compiler* originally written by Fabrice Bellard, and is
extended by Per Nordlöw with mostly D language features such as
slices, dynamic arrays, UFCS, `foreach`, `defer`, `immutable`,
compile-time `version`/`debug` conditionals, and many other features.

There are plans for syntactic extensions such as

(`$if`), a shorter `static if`.

The principal goal of TinyPC (TPC) is to become a compiler of multiple
languages with modern features found in statically-typed system
languages such as D, Zig, Rust, and C3—without sacrificing
extraordinary default and debug-build compilation and run-time
performance inherited from TinyCC. Enabling outstandingly low-latency
incremental development loops for both human engineers and AI agents
compared to all other statically typed (system) languages including D.

This ambitious scope has now become possible by the leverage of AI
coding agents for a single developer.

## Highlights

- **Small!** TPC can compile and execute C code almost everywhere, for
  example on rescue disks and embedded systems.

- **Fast!** TPC generates machine code for i386, x86_64, ARM, AArch64,
  and RISC-V 64.  It compiles and links roughly ten times faster than
  `gcc -O0`.

- **Self-hosting!** TPC can compile itself, which is also used as a
  stress test for the code generator.

- **Safe!** An optional memory and bounds checker can be enabled with
  `-b`.  Checked code can be mixed freely with standard code.

- **Direct execution!** C source can be compiled and run without a
  separate linking or assembly step.  A full C preprocessor is
  included.

- **Scripting!** Add `#!/usr/local/bin/tcc -run` to the first line of a
  C source file, make it executable, and run it like a shell or Perl
  script.

- **Polyglot!** In addition to C, TPC supports a large and growing set
  of D-style language extensions, plus an integrated assembler,
  linker, archiver, and Windows `def` file generator.

## Supported targets

TPC can generate code for:

- i386
- x86_64
- ARM (including EABI and VFP variants)
- AArch64
- RISC-V 64
- TMS320C67xx

It supports ELF (Linux, BSD, and other Unix-like systems), PE/COFF
(Windows), and Mach-O (macOS) output.

## Building and installing

On Linux, BSD variants, and macOS hosts:

```sh
./configure
make -j$(nproc)
make test
make install
```

On BSD hosts use `gmake` instead of `make`.  For Windows, see
[`win32/tcc-win32.txt`](win32/tcc-win32.txt).

### Building with Ninja (parallel path)

A Python + Ninja build is provided as a parallel, faster-incremental
alternative to GNU Make.  It produces the same native `tcc` and
`libtcc1.a`:

```sh
./configure --ninja
ninja -C build
ninja -C build test
```

`./configure --ninja` runs `./configure` as usual and then invokes
`tools/build`, which writes `build/build.ninja`.  `ninja -C build test`
delegates to the existing test suite.  The Make-based build remains the
primary, fully-featured path (PGO, cross-targets, installation); the
Ninja path is intended for fast local iteration.

The TPC sources require a C11 host compiler.  The Makefile builds with
the GNU C11 dialect (`-std=gnu11`) via the `CSTD` variable so that
features from later C standards are not used accidentally.  See
[`CodingStyle.md`](CodingStyle.md) for the accepted language extensions and
language rules.  An explicit `-std=` in `CFLAGS` takes precedence; to
override the default entirely:

```sh
make CSTD=-std=gnu99
```

`makeinfo` is needed to build the documentation.  By default, `tcc` is
installed in `/usr/local/bin`.  Run `./configure --help` for the
available configuration options.

### Profile-guided optimization

TPC has automated PGO targets for Clang and GCC:

```sh
make pgo PGO_CC=clang
make pgo PGO_CC=gcc
```

`make pgo` builds an instrumented compiler, trains it by compiling TPC
itself, merges the profile, and produces a `-O3 -march=native` build.
It finishes with `make pgo-bench`, which reports self-compile
throughput in Mlines/s and the Valgrind cachegrind instruction count
(`I refs`).  `PGO_MARCH` defaults to `native`; on x86, `-mno-avx512f`
is added so that Valgrind can decode the generated code.

`pgo-bench` keeps the best of five runs.  `-bench` measures in whole
milliseconds, which is about 5% of a ~20 ms self-compile, so the
throughput figures move in steps of that size; the instruction count
is exact and is the better signal for small changes.

### Host compiler choice matters for runtime speed

The compiler used to *build* TPC strongly affects the speed of the
resulting `tcc` binary.  On an AMD Ryzen AI 7 350 (Linux x64), full
`make pgo` builds of commit `faa148ff` measure:

- `PGO_CC=gcc`: about 5.3 Mlines/s and 207 M executed instructions
- `PGO_CC=clang`: about 4.8 Mlines/s and 235 M executed instructions
- `PGO_CC=clang` with `-ftls-model=initial-exec`: about 4.8 Mlines/s
  and 223 M executed instructions

The reason is thread-local storage.  Since commit `bc86ac3c` the
mutable compiler state lives in TLS (`TCC_TLS`) as groundwork for
[multi-threaded compilation](#thread-safety); Clang compiles this
position-independent executable with the general-dynamic TLS model, so
nearly every access to preprocessor or generator state becomes a
`__tls_get_addr` call.  That costs roughly 11% more instructions on a
fixed input, concentrated in the lexer and expression parser hot spots
(`next_nomacro`, `next`, `next_argstream`, `unary`), whereas GCC picks
a cheaper TLS model and pays only about 1%.  Building with
`-ftls-model=initial-exec` removes the calls and brings a fixed input
back to within about 2% of its pre-TLS instruction count.

Until the hot thread-local state is made cheaper to access, **use GCC
as the host compiler when the speed of the resulting binary matters**:

```sh
make pgo PGO_CC=gcc
```

Numbers are machine specific; compare on your own host with
`make pgo-bench`.  Every recorded result is kept in
[`bench/pgo-history.tsv`](bench/pgo-history.tsv).

## Using TPC

Compile and link a program:

```sh
tcc -o myprog a.c b.c
```

Compile only:

```sh
tcc -c a.c
```

Compile and run immediately:

```sh
tcc -run app.c
```

The `.c` suffix may be omitted when no file of the given name exists.
When the resulting file is run directly, as in the common case below,
TPC does not leave an executable behind:

```sh
tcc app          # equivalent to tcc -run app.c
```

Show the version, more options, compilation statistics, or the
preferred source formatting rules:

```sh
tcc -v
tcc -hh
tcc -bench
tcc -style
```

`tcc -style` prints the formatting rules used by the project,
including the rule that D conditional keywords are spelled without a
space before the parenthesis: `version(...)` and `debug(...)`, not
`version (...)` or `debug (...)`.

### Documentation

The reference manual is [`tcc-doc.html`](tcc-doc.html), generated from
[`tcc-doc.texi`](tcc-doc.texi).  Windows-specific information is in
[`win32/tcc-win32.txt`](win32/tcc-win32.txt).  The coding conventions
are documented in [`CodingStyle.md`](CodingStyle.md), and the project's
planned work is tracked in [`TODO.md`](TODO.md).

For C structure layout and alignment, see Eric S. Raymond's
[*The Lost Art of C Structure Packing*](http://www.catb.org/esr/structure-packing/).

## Language modes

The default is C with a number of compatible C superset extensions
always enabled.  Select the D-style dialect explicitly with:

```sh
tcc -lang=c ...    # default C dialect
tcc -lang=d ...    # enable D-specific grammar and semantics
```

`make` also builds a `tdc` wrapper (a symlink to `tcc`, or `tdc.bat`
on Windows) that implies `-lang=d`, so D sources need no extra flag:

```sh
./tdc app.d       # same as tcc -lang=d app.d
```

The following features are available in all modes:

- `defer` for scope-based cleanup in C functions
- `__attribute__((nocopy))` for non-copyable struct types
- `static assert(expr[, message])` and its `$assert(expr[, message])`
  alias
- automatic `x.field` member access through a pointer to a struct to
  ease transitioning between a passing a parameter by value vs
  reference
- `auto` type inference for local variables from their initializer
- D-style `@safe`, `@system`, `@trusted`, and `@pure` attributes
- D-style slices, slice expressions, dynamic arrays, `foreach`,
  `alias`, manifest constants, `immutable`, and `unittest` blocks
- type tuples `(T1, T2, ...)` with positional `t[i]` access

`-lang=d` additionally enables the D-specific forms that
would otherwise change the meaning of valid C or reserve ordinary
identifiers:

- element-wise compound assignment on slice ranges (`a[..] += 1`)
- empty subscripts as full slices (`a[] = b[]`)
- compile-time value tuples (`$(...)`), `$if`, and `$foreach`
- `scope(exit)` as an alias for `defer`
- `version(...)` and `debug(...)` conditional statements
- the `.sizeof` property on types and expressions


In C mode, names such as `version` and `debug` remain ordinary
identifiers so that existing C code continues to compile unchanged.

## Type tuples

A parenthesised, comma-separated list of types is a tuple type:

```c
typedef (int, char) Pair;     /* 2-tuple                          */
typedef (int,) One;           /* 1-tuple, trailing comma needed */
typedef ((int, char), double) Nested;

Pair p = {1, 'a'};
Pair f(Pair v) { return v; }  /* tuple parameter and return type  */

p[0]                          /* positional access, constant index */
sizeof(Pair)                  /* 8: int then char, struct layout   */
_Alignof((int, char))
typeof(p)
_Generic(p, Pair: 1, default: 0)
__builtin_types_compatible_p(Pair, (int, char))   /* 1 */
```

- Tuples work in declarations, typedefs, `alias`, struct fields,
  parameters and return types, and in both `-lang=c` and `-lang=d`.
- Layout, alignment, initialisation, assignment and the calling
  convention are exactly those of the equivalent unnamed struct, so
  a tuple can cross a translation unit that declares the same
  values as a struct.
- Tuple types are structural: `(int, char)` is compatible with
  itself and with a typedef of itself, but not with a plain struct
  of the same layout.
- `t[i]` needs a constant index; it is an lvalue like a `.member`
  access.  `()` is not a valid tuple type.
- A cast to a tuple type is rejected; use an initializer or a
  compound literal such as `(Pair){1, 'a'}` instead.
- `(a, b)` where `a` and `b` are values remains a comma expression
  everywhere except as the pattern of a destructuring assignment
  `(a, b) = ...`; a group is only a tuple type when every element
  parses as a type, which no valid expression does.
- Tuples carry no debug information (M1).

### Value Tuples (Compile-Time and Run-Time)

In `-lang=d` mode, value tuples provide multi-value constructors and
literals:

- **Compile-Time Value Tuples (`$(...)`):**
  ```c
  enum { N = $(1, 2)[0] };            /* 1 */
  static int table[$(4, 8)[1]];       /* 8 elements */
  $assert($(4, 8)[1] == 8);
  static const int k = $(2 + 3);      /* 5 */
  ```

- **Run-Time Value Tuples (`(...)`):**
  Accepted in tuple-expected contexts (initializers, assignments, arguments,
  returns, and destructuring):
  ```c
  (int, int) t = (x, y);
  return (x, y);
  auto (a, b) = f();          /* destructuring initializer */
  ```

- **Destructuring Assignment:**
  A parenthesized pattern of lvalues followed by `=` stores the
  elements of a tuple into them.  This works in both language modes:
  ```c
  (a, b) = (b, a);            /* swap                            */
  (a, b, c) = (b, c, a);      /* rotate                          */
  (s.x, arr[i]) = (1, 2);     /* fields and array elements       */
  int z = ((a, b) = (3, 4))[0];    /* the right side is the value */
  ```
  The right side is evaluated into a temporary before any destination
  is written, so a swap really swaps.  The pattern is only recognised
  when the group has a top-level comma and is followed by `=`;
  otherwise `(a, b)` keeps its comma-expression meaning.

- **Static Foreach over Tuples (`$foreach`):**
  ```c
  $foreach (x; $(1, 2, 3)) {
      /* compiled per element at compile time */
  }
  ```

See [`tasks/support_tuples.md`](tasks/support_tuples.md) for full design
details.


## D-style language extensions

### Slices

Slices are a pointer/length pair.  Both the D-style type syntax and a
C-compatible spelling are accepted:

```c
int[]  s1;       /* D-style slice type */
int[..] s2;      /* C-compatible slice type */
int s3[..];      /* suffix form, usable in parameters */

typedef int[..] Ints;

int a[5] = {10, 20, 30, 40, 50};
Ints s = a[1..4];   /* length 3, pointing at a[1] */
```

Slice expressions include:

```c
a[lo..hi]     /* half-open range */
a[..]         /* entire array */
a[]           /* entire array, D-style empty subscript (-lang=d) */
"text"[..]    /* slice of a string literal; length excludes '\0' */
a[$-1]        /* '$' is the length of the subscripted array or slice */
```

Slices expose `.ptr` and `.length`:

```c
int *p = s.ptr;
size_t n = s.length;
```

Arrays and string literals convert implicitly to slices; slice values
can be stored in structs, passed to and returned from functions, and
compared with `__builtin_types_compatible_p`.

Slices compare by contents: `==` and `!=` first check that the lengths
match and then compare element by element.  Plain arrays keep the C
meaning and compare as pointers, so write `a[..] == b[..]` to compare
two arrays element-wise.

### Slice assignment and element-wise operations

Whole slices can be copied or filled:

```c
dst[..] = src[..];   /* copy */
dst[]   = src[];     /* same, D-style empty subscript */
dst[..] = 42;        /* fill every element */
dst[1..4] += 10;     /* element-wise compound assignment (-lang=d) */
```

All integer compound assignments are supported element-wise (`+=`,
`-=`, `*=`, `/=`, `%=`, `&=`, `|=`, `^=`, `<<=`, `>>=`).  The result
of a slice assignment is the destination slice.

When source and destination lengths differ, the behavior is selected
with `-fslice-len-mismatch=MODE`:

- `abort` (default): report an error
- `copy`: copy the common prefix
- `ignore`: perform no copy

### Dynamic arrays

Slices support D-style dynamic-array operations:

```c
typedef int[..] Ints;

Ints a = {0};
a ~= 10;             /* append */
a ~= 20;
Ints b = a ~ a;      /* concatenate */
Ints c = a.dup;      /* mutable duplicate */
Ints d = a.idup;     /* immutable duplicate */
a.resize(100);       /* grow or shrink; growing preserves elements */
```

`~=` reallocates as needed and integrates with the bounds checker.
`.dup` and `.idup` produce independent copies; `~` concatenates two
slices (or arrays converted to slices).  `.length` and `.ptr` are the
fields of the slice header; use `.resize(n)` when a length change
should reallocate the element storage.

### Foreach

Iterate over arrays, slices, VLAs, string literals, and integer
ranges:

```c
foreach (v; a)              /* value */
foreach (i, v; a)           /* index and value */
foreach_reverse (v; a)      /* reverse order */
foreach_reverse (i, v; a)

foreach (i; 0 .. 10)        /* integer range, half-open */
foreach (int i; 10 .. 13)   /* explicit loop variable type */
```

The loop variable is a copy, `break` and `continue` work as expected,
the aggregate expression is evaluated exactly once, and explicit
types may be given for both the index and the value.

### Type-first array declarations

D-style type-first array declarations are accepted:

```c
int[4] a;                /* same as int a[4] */
int[] b;                 /* unsized array */
typedef int[4] Four;
typedef int[..] IntSlice;
const int[2] c;
unsigned[2] u;
int[2] x, y;             /* the suffix applies to every declarator */
int[2][3] matrix;
```

### The `.sizeof` property

In `-lang=d` mode, `.sizeof` yields the size of a type or of an
expression, in the style of D:

```c
int.sizeof;              /* sizeof(int) */
double.sizeof;
struct Point.sizeof;
int[10].sizeof;
int*.sizeof;
x.sizeof;                /* sizeof x */
```

C mode keeps the `sizeof` operator and does not accept this form.

### Uniform Function Call Syntax (UFCS)

A call of the form `x.f(args)` is rewritten to `f(x, args)` when `f`
is not a member of `x`:

```c
int add(int a, int b) { return a + b; }

int x = 5;
printf("%d\n", x.add(10));   /* calls add(x, 10) */
```

### Aliases

D-style `alias` declarations create type aliases and interoperate with
`typedef`:

```c
alias Int = int;
alias IntPtr = int *;
alias IntArr = int[3];
alias IntSlice = int[..];
alias Fn = int (*)(int);
alias FnType = int(int);
```

Aliases may also be declared at block scope.

### Manifest constants

Compile-time constants may be declared with `enum`:

```c
enum x = 42;
enum int y = 100;
enum long z = 1000L;

void f(void)
{
    enum local_const = 999;
}
```

### Automatic type inference

A local variable declared with `auto` and an initializer takes the
type of that initializer:

```c
auto x = 42;       /* int */
auto p = &x;       /* int * */
```

Alternatively, the `SYM := EXPR;` shorthand operator can be used as a
direct alias for `auto SYM = EXPR;` (inspired by Go, Zig, and Julia):

```c
x := 42;           /* int x = 42; */
result := calculate();
```

Re-declaration of an existing symbol in the same scope with `:=` is
rejected with an error.

The initializer is mandatory; a declaration such as `auto x;` is
rejected with `declaration with 'auto' has no initializer`.
### Immutable

The `immutable` qualifier declares deeply immutable objects:

```c
immutable int g = 42;
immutable int ga[3] = {1, 2, 3};

struct S {
    int a;
    immutable int b;
};

immutable struct S s = {1, 2};
```

Immutability is transitive through aggregates, pointers, and arrays.
Converting a mutable pointer to an immutable one is allowed; dropping
`immutable` requires an explicit cast.  Assignments that would violate
immutability are diagnosed at compile time.

### Safety and purity attributes

D-style attributes are accepted on function declarations and
definitions in every language mode:

```c
int add(int x) @safe { return x + 1; }
int twice(int x) @pure;

void f(void)
{
    @trusted {
        /* direct memory access is allowed here */
    }
    @trusted raw_call();
    @trusted for (int i = 0; i < 10; i++) {
        /* trusted loop body */
    }
}
```

The `-lang=d` mode activates `@safe` by default.  Function attribute
inference, opposite to D, happens for non-templated functions, as this
compiler currently has no plans for templates nor type-aware macros
(though some plan for templates and macros will be added later on).

The `@safe` is enforced for functions and blocks: pointer
arithmetic and pointer indexing are rejected.  `@trusted` and
`@system` mark statements, loops (`for`, `foreach`), and blocks as
exempt from those checks.  `@pure` enforces functional purity and
determinism (disallowing global/static variable access and
non-deterministic pointer arithmetic or relational comparisons, while
permitting pointer dereferencing and null checks).  `@nodet` serves as
an opt-out for determinism requirements.  Detailed semantics are
documented in
[tasks/pure_attribute_semantics.md](tasks/pure_attribute_semantics.md)
and
[tasks/deterministic_attribute.md](tasks/deterministic_attribute.md).

### Defer and scope(exit)

The `defer` statement registers cleanup code that runs when the
enclosing scope exits, including on early `return`:

```c
void f(void)
{
    FILE *fp = fopen("data.txt", "r");
    defer fclose(fp);
    defer {
        puts("leaving f");
    }
    /* ... */
}
```

Deferred statements run in last-in, first-out order, one scope at a
time:

```c
{
    defer puts("outer");
    {
        defer puts("inner 1");
        defer puts("inner 2");
    }
}
```

In `-lang=d` mode, `scope(exit)` is accepted as an alias for `defer`.
`scope(success)` and `scope(failure)` are recognised but currently
rejected with a diagnostic, because they need error-status tracking
that the compiler does not implement yet.

### Unittest blocks

D-style `unittest` blocks contain tests that are compiled and run only
when `-unittest` is given:

```c
static int twice(int x) { return 2 * x; }

unittest {
    assert(twice(3) == 6);
}

int main(void)
{
    return 0;
}
```

Without `-unittest`, the blocks are skipped and produce no code.
`version(unittest)` reflects whether unit tests are enabled.

### Version and debug conditionals

D-style compile-time conditionals are supported:

```c
version(Foo) {
    /* compiled when Foo is defined */
} else version(Bar) {
    /* ... */
} else {
    /* ... */
}

version(2) { /* compiled when the version level is at least 2 */ }

version(none) { /* never compiled */ }
version(all)  { /* always compiled */ }

debug {
    /* compiled when the debug level is nonzero */
}

debug(Lexer) {
    /* compiled when the Lexer debug identifier is defined */
}

debug(2) {
    /* compiled when the debug level is at least 2 */
}
```

Identifiers and levels can also be specified in the source or on the
command line:

```c
version = Foo;
version = 2;
debug = Lexer;
debug = 3;
```

```sh
tcc -version=Foo -version=2 -debug -debug=Lexer -debug=2 ...
```

`version = none;` cannot make `version(none)` true, and
`version = all;` cannot make `version(all)` false.  Built-in version
identifiers include `D_Version2`, `assert`, `unittest`, the target
architecture (`X86`, `X86_64`, `ARM`, `AArch64`, `RISCV64`), the
platform (`Windows`, `Win32`, `Win64`, `OSX`, `Darwin`, `linux`,
`FreeBSD`, `OpenBSD`, `NetBSD`, `DragonFlyBSD`, `Android`, `Posix`),
and endianness (`LittleEndian`, `BigEndian`).

In D mode, `version` and `debug` are conditional keywords, not
variables; their names are not visible to ordinary identifier lookup.

### Static assertions and `$assert`

The standard C11 `static assert` form is supported, with an optional
message, and `$assert` is accepted as a shorter alias:

```c
static assert(sizeof(int) == 4);
static assert(sizeof(void *) == 8, "expected 64-bit pointers");

$assert(sizeof(int) == 4);
$assert(sizeof(void *) == 8, "expected 64-bit pointers");
```

### Runtime `assert` with custom message

In D mode (`-lang=d`), runtime `assert` is supported with an optional message string:

```c
assert(cond);
assert(cond, "descriptive failure message");
```

If the condition fails at runtime, it prints an assertion failure message and aborts.
### Pointer-to-struct member access

For a pointer to a struct, `x.field` is accepted as a synonym for
`x->field`, including chained access and compound assignment:

```c
struct Point { int x, y; };
struct Node { struct Node *next; int val; };

struct Point p = {10, 20};
struct Point *ptr = &p;

ptr.x = 100;        /* same as ptr->x = 100 */
ptr.x += 5;
printf("%d\n", ptr.x);

struct Node *n = /* ... */;
printf("%d\n", n.next.val);   /* same as n->next->val */
```

### `__attribute__((nocopy))`

A struct type may be marked non-copyable:

```c
struct UniqueRes {
    int fd;
} __attribute__((nocopy));

struct UniqueRes r1 = {42};
struct UniqueRes r2 = r1;   /* compile-time error */
```

Non-copyable structs cannot be passed by value (including through
K&R and variadic parameter lists), initialized by copy, or returned by
value.  This makes it possible to model move-only resources such as
file descriptors and locks.

### `@nocopy` on Instance Declarations

In D language mode (`-lang=d`), `@nocopy` can also be applied to individual instance declarations and variables (both struct and primitive types such as raw resource handles or `int fd`):

```c
@nocopy File f1 = open_file("log.txt");
File f2 @nocopy = open_file("data.bin");

// f1 = f2;        /* Error: cannot copy @nocopy instance 'f2' */
// process(f1);    /* Error: cannot copy @nocopy instance 'f1' */
process(&f1);      /* OK: passing pointer / address */
```

This allows developers to enforce non-copyability at the variable or instance level (useful for raw file descriptors, security nonces, or unique handles) without requiring a custom wrapper struct or disabling copying globally across all instances of a type. Moves, initial construction, and explicit address-taking (`&x`) are fully permitted.

## Tooling and diagnostics

### Debug dumps

The front end can dump its internal state while parsing:

```sh
tcc -fdump-types  ...
tcc -fdump-decls  ...
tcc -fdump-vstack ...
```

`-fdump-vstack` prints the value stack on every mutation, which is
useful when debugging the parser and code generator.

### Coverage and uncovered-line warnings

`-ftest-coverage` instruments the program and writes a `.tcov`
section.  `-Wuncovered` reports source lines that were never executed:

```sh
tcc -ftest-coverage -Wuncovered -run tests.c
```

The warning can be promoted to an error in the usual way:

```sh
tcc -ftest-coverage -Werror=uncovered -run tests.c
```

`-Wno-uncovered` disables the warning.  The `.tcov` section layout is
kept stable so that existing coverage tooling continues to work.

### Other useful options

- `-b`: enable the memory and bounds checker (implies `-g`)
- `-bt[N]`: link with backtrace support and show up to `N` callers
- `-g`, `-gdwarf[-x]`: generate stab or DWARF debug information
- `-M`, `-MM`, `-MD`, `-MMD`, `-MF`: generate make dependency files
- `-E`: preprocess only
- `-ar`: create or inspect a static library
- `-impdef`: create a Windows `.def` file from a DLL

## Thread safety

The mutable state of the preprocessor, code generator, assembler, and
target backends is thread-local (`TCC_TLS`) rather than global.  This
is deliberate groundwork for multi-threaded compilation: an embedder
that compiles independent `TCCState`s from several threads must not
have those threads race on shared parser and generator globals, which
is exactly what would happen if the state stayed global.

Concurrency is not enabled end to end yet.  A little state is still
process-global, notably the library list and the reallocator hook
(`tcc_set_realloc()`), so `tcc_compile()` serialises compilations with
a semaphore.  `TCC_TLS` can be overridden: it is empty on targets
whose runtime does not support TLS (ARM, C67, Mach-O), and `tcc -run`
does not support TLS, so the self-compilation tests pass `-DTCC_TLS=`
when they run `tcc.c`.

The design is not free.  Clang compiles the position-independent `tcc`
binary with the general-dynamic TLS model, which turns hot accesses to
the thread-local state into `__tls_get_addr` calls.  This is the
performance difference described under [Host compiler choice matters
for runtime speed](#host-compiler-choice-matters-for-runtime-speed);
GCC and `-ftls-model=initial-exec` avoid most of that cost.

## Performance work

The compiler has been profiled and optimized with hardware performance
counters and Valgrind cachegrind.  Notable changes include:

- SIMD-accelerated string-literal and line-comment scanning with
  `memchr`
- faster whitespace skipping and macro/non-macro dispatch in the
  lexer, including branch-prediction hints
- inlined token-string append operations
- PGO builds with a 25-iteration training run as the sweet spot
- `make pgo-bench` for repeatable throughput and instruction-count
  measurements; `bench/pgo-history.tsv` records one row per commit and
  host compiler
- `PT_GNU_STACK` program headers for non-executable stacks

When changing compiler sources, the repository's verification sequence
is:

```sh
./configure && make -j$(nproc) -B && make -j$(nproc) test
make -j$(nproc) pgo PGO_CC=clang
```

## Project history and naming

The project started as the *Tiny C Compiler* (TinyCC) by Fabrice
Bellard.  This fork adds a broad set of D-inspired language features
and C superset extensions, so it has been renamed to the **Tiny Poly
Compiler** (TPC, also written TinyPC).  The rename covers user-visible
documentation, help text, and comments; the `tcc` executable and the
`TCC`/`TCC_*` internal identifiers are intentionally unchanged for
compatibility with existing build scripts, bindings, and embedders.

## License

TPC is distributed under the GNU Lesser General Public License.  See
[`COPYING`](COPYING) for the full license text.  Parts of the code
have been relicensed under the MIT license; see
[`RELICENSING`](RELICENSING) for details.

Original author: Fabrice Bellard.  D-language extensions and other
features in this fork: Per Nordlöw.
