# Task: Tuple Support (type tuples, compile-time and run-time value tuples)

Add tuples to the language in three stages, in this order:

1. **P1 -- type tuples.**  `(int, char)` is a type.
2. **P2 -- compile-time value tuples.**  `$(1, 'a')` is a tuple of
   values computed at compile time.
3. **P3 -- run-time value tuples.**  `(a, b)` builds a tuple at run
   time, in a context that already expects a tuple.

Compile-time and run-time tuples have **separate spellings**: `$`
marks compile time throughout the family, so `$(...)` requires
compile-time elements, while unmarked `(...)` is a run-time literal.
`(...)` is accepted only where the expected type is already a tuple
(initializer, assignment, argument, return, cast operand,
destructuring), so a comma expression is never reinterpreted.  The
resulting type is the same either way.

The feature is the substrate the reflection proposal in `TODO.md:151`
needs: `$fields(T)` and `$members(T)` are compile-time tuples, and
`$for`/`$foreach` over them is only possible once tuples exist.

## The `$` compile-time family

`$` marks compile time.  Tuples are one member of that family, so the
spelling has to be fixed with the rest of it:

| form | meaning | status |
| --- | --- | --- |
| `$(e)` | evaluate `e` at compile time; the result is the value of `e` | this task (M2) |
| `$(e1, e2, ...)` | tuple of compile-time constants | this task (M2) |
| `$(e,)` | compile-time 1-tuple | this task (M2) |
| `(e1, e2, ...)` | run-time tuple, only where a tuple is expected | this task (M3) |
| `(e,)` | run-time 1-tuple, same restriction | this task (M3) |
| `$if (c) ...` | static `if` | M2 (shared with the reflection task) |
| `$foreach (x; t) ...` | static `foreach` over a tuple | M4 (shared with the reflection task) |
| `$assert(...)` | static assert | exists (`tccgen.c:10640`) |
| `$tupleof(T)` | member types of struct `T` as a type tuple (D's `T.tupleof`); the type-level projection of `$fields(T)` | future (open question 15) |
| `$run(...)`, `$run { ... }` | compile-time function execution | `tasks/compile_time_function_execution.md` |
| `$T`, `$param` | template type/value parameter | future; out of scope |
| `$EXPR` | **not adopted** | see below |

The rule that makes this work is: **`$` is a compile-time operator
only when the next token is `(`**.  Otherwise `$` is an ordinary
identifier character (`tcc.h:811`, on by default like gcc and clang),
so `$if`, `$x` and `$T` are single identifier tokens, not `$` plus a
word.  A `$`-word is recognised contextually, exactly as `$assert`
is today (`tccgen.c:10655`): the parser inspects the identifier text
and the following token (`(` for `$(`, `$if` and `$foreach`; `{` for
`$run`) and only then commits.

Consequences:

- **No lexer change.**  `tccpp.c:2696` already yields the identifier
  `$` when `$` is followed by `(`, because `(` is not an identifier
  character.
- **No identifier regression.**  `int $if;`, `int $x;` and a function
  named `$` remain valid; no `$`-word becomes a reserved word.
- **Uniform spelling.**  Compile-time constructs are `$(...)` and
  `$word(...)`, never a bare `$EXPR`.
- **Separate value spellings.**  `$(...)` builds at compile time and
  `(...)` builds at run time.  Neither is sugar for the other, and
  `$(...)` is never accepted with run-time elements.

### Why bare `$EXPR` is not adopted

A unary `$EXPR` operator requires `$` to stop being an identifier
character, because `$x` would have to lex as `$` `x` instead of as
the single identifier `$x`.  In `-lang=d` that would re-lex every
existing `$`-prefixed identifier, break the textual `$assert` match,
and diverge from gcc/clang.  It is also the only form that collides
with D's opDollar, which this fork implements (`gen_dollar()`,
`tccgen.c:7144`; `tests/tests2/323_d_dollar.c`).  `$(EXPR)` costs two
characters and keeps both properties.  Add the bare form later only
with a dedicated lexer mode, a migration path for `$assert`, and an
opDollar rule.

## Goal

- **P1:** parse `(T1, T2, ..., Tn)` as a type in declarations,
  typedefs, `alias`, fields, parameters and return types; give it a
  deterministic, struct-compatible layout, initializer support,
  element access `t[i]`, and struct-compatible assignment and
  calling convention.
- **P2:** parse `$(e1, e2, ..., en)` as a tuple whose elements must
  all be compile-time constants, and allow constant indexing,
  `sizeof`, `typeof`, `_Generic`, `static assert` and `$assert` to
  consume it.  `$(e)` is the compile-time value of `e`; `$(e,)` is a
  compile-time 1-tuple.
- **P3:** parse `(e1, e2, ...)` as a run-time tuple literal where the
  expected type is already a tuple, and allow tuple-typed variables
  and temporaries, tuple arguments and returns, and destructuring
  `auto (a, b) = expr;`.
- Keep every valid C and D program meaning exactly what it means
  today, in particular comma expressions and opDollar (`a[$]`).
- Reuse the existing struct machinery for layout, assignment, ABI,
  initializers and debug info instead of adding a parallel type kind.

Non-goals: tuple arithmetic, tuple comparison in P1/P2 (see M5),
variadic tuples, tuple comprehensions, and a library `Tuple` type.
An inferred-type tuple local (`auto t = (a, b);`) is not provided,
because `(a, b)` is a comma expression there; see
*Run-time value tuples*.  Templates are out of scope: this compiler
has no instantiation, mangling or generic machinery, so `$T` and
`$param` are at most an agreed naming convention for a future
feature.  `$if`, `$foreach` and `$run` are specified elsewhere; this
task only fixes the tuple syntax they share.

## Motivation

- **Multiple return values.**  Today a function returning two values
  must use an out-parameter, a global, or a struct with a tag.  A
  tuple makes `(int, char) f(void)` and `auto (i, c) = f();` possible
  without naming a helper struct.
- **Reflection.**  `TODO.md:151` proposes `$fields(T)` returning a
  tuple of reflection nodes and `$for`/`$foreach` over them.  Without
  a tuple type those results have to be encoded as arrays of
  homogeneous nodes, which loses per-field types.
- **D compatibility.**  D's `std.typecons.Tuple` is a library type
  built on templates.  tcc has no templates, so a built-in tuple is
  the shortest path to the same ergonomics.
- **Slices are the precedent.**  The fork already represents a D
  slice as an anonymous `VT_STRUCT` with `SymAttr.is_slice`
  (`make_slice_type()`, `tccgen.c:5564`; `is_slice_type()`,
  `tccgen.c:3773`).  Tuples should follow that path rather than the
  legacy `VT_SLICE` basic type, which is now only read in
  `type_size()` (`tccgen.c:3741`) and `find_field_opt()`
  (`tccgen.c:4504`) and is never constructed.

## Priorities and scope

| prio | construct | user-visible result | depends on |
| --- | --- | --- | --- |
| P1 | type tuple | `typedef (int, char) Pair;` `Pair p;` `p[0]` | representation (M0/M1) |
| P2 | compile-time tuple | `enum { N = $(1, 2)[0] };` | P1 (M2) |
| P3 | run-time tuple | `(int, int) t = (x, y);` `return (a, b);` | P1 (M3) |

P1 is useful on its own: a tuple-typed struct field, parameter or
return value works as soon as the type exists.  P2 is what the
reflection proposal consumes.  P3 is the ergonomic layer on top, and
does not depend on P2: run-time tuples need the type, not the
compile-time constructor.

## Decision record

- **D1 -- priority order.**  P1 type tuples, P2 compile-time value
  tuples, P3 run-time value tuples.  Each milestone is independently
  shippable.
- **D2 -- representation.**  A tuple is an anonymous `VT_STRUCT` with
  a new `SymAttr.is_tuple` bit and positional anonymous fields, not a
  new `VT_*` basic type.  `VT_BTYPE` is a saturated 4-bit field
  (`tcc.h:1118`; all 16 values 0..15 are taken, and the next bit,
  `0x0010`, is `VT_UNSIGNED`), so a new basic type means renumbering
  every type flag and auditing every backend switch.  See
  *Internal representation decision*.
- **D3 -- identity.**  Tuple types are structural and anonymous.  No
  tag name is generated from the element types; `compare_types()`
  compares element-wise, and an optional hash-consing table keyed by
  element type identity canonicalises repeated occurrences.  A
  printable spelling `(int, char)` is synthesised by `type_to_str()`
  on demand.
- **D4 -- language modes.**  Type tuples and run-time tuple literals
  are enabled in all modes: `(T1, T2)` was invalid C before, so no
  valid C program changes meaning, and `(...)` is only read as a
  tuple where the expected type is already a tuple -- a type that can
  only come from the new syntax.  Compile-time tuples are `-lang=d`
  only, because `$` is a valid identifier character (`tcc.h:811`)
  and `$(x)` currently parses as a call to a function named `$`.  A
  `$`-word only specialises when `(` follows, so `$x` and `$T` stay
  identifiers and `$if`/`$foreach` remain usable as names.  See *The
  `$` compile-time family* and *Language mode decision*.
- **D5 -- separate spellings.**  `$` marks compile time, so
  `$(...)` builds a tuple of compile-time constants and `(...)`
  builds a run-time tuple.  The two are not aliases: an element of
  `$(...)` must be a compile-time constant, and `(...)` is not
  accepted outside a tuple-expected context.  `${...}` is not
  adopted: braces already mean blocks, initializers and
  `$run { ... }`.  A bare `$EXPR` is not adopted either: `$` is an
  identifier character, so it needs a lexer mode and collides with
  opDollar.
- **D6 -- the `(...)` context rule.**  `(...)` is a value tuple only
  in a context that already expects a tuple (initializer,
  assignment, argument, return, cast operand, destructuring
  pattern).  Everywhere else it stays a comma expression.  This
  never changes the meaning of a valid program, because the expected
  type in those contexts is a tuple, and a tuple type cannot appear
  in code that predates this feature.  The rule is therefore safe in
  `-lang=c` as well, not only in `-lang=d`.
- **D7 -- arity spelling.**  A 1-tuple always needs a trailing
  comma: `(T,)` is a 1-tuple type and `(T)` stays a parenthesised
  type/cast; `$(e,)` and `(e,)` are 1-tuples and `$(e)`/`(e)` are
  the value of `e`.  This matches Python and Swift and keeps the
  compile-time and run-time spellings in agreement about arity.
  `()` is an error in M1 (a unit type can be added later).
- **D8 -- M1 parsing probe and field naming.**  The hook lives inside
  `parse_btype()`'s specifier loop (`tok == '(' && !typespec_found`),
  so `typedef (int, char) P;`, `(int, char) x;` and
  `sizeof((int, char))` all take the same path; `unary()` only builds
  the remaining elements after its own `parse_btype()` call returned
  the first one.  The group is scanned into a `TokenString` and
  replayed to *probe* it: commit only if it has a top-level comma and
  every top-level element parses as a complete abstract type.
  Committing on "the first token can start a type" alone is unsound:
  the elements of `((void)0, (T){0})` in
  `tests/tests2/94_generic.c:36` look like types, yet the group is a
  comma expression.  A group whose elements all parse as types can
  never be a valid expression, so the probe never changes the meaning
  of a valid program.  On failure the group is replayed unchanged and
  the macro stack is unwound to its saved top.  Elements are stored
  as named struct fields `$0`, `$1`, ... (`tok_alloc_const()`) rather
  than anonymous ones, because anonymous fields break
  `decl_designator()`'s bitfield padding skip and `check_fields()`'s
  `table_ident` lookup; `$` is an identifier character, so these
  names are not spellable from source.

## Surface syntax

### Type tuples

```c
typedef (int, char) Pair;

Pair p = {1, 'a'};          /* struct-style initializer */
int  i = p[0];
char c = p[1];

Pair f(void);               /* ABI is that of struct { int; char; } */
void g(Pair p);
Pair *q = &p;

sizeof(Pair);               /* 8 on the usual ABIs */
_Alignof(Pair);

((int, char)){1, 'a'};      /* compound literal */
```

Rules:

- An element is a full type expression: `(int *, char[3])`,
  `(const int, char)`, `((int, char), int)`.
- `void`, function types, VLAs, incomplete types and storage classes
  (`static`, `extern`, `typedef`, `inline`, `_Thread_local`) are
  rejected as elements, mirroring the checks in `struct_decl()`
  (`tccgen.c:5049`-`5060`).  `struct_layout()` does not reject them
  on its own (`type_size(VT_VOID)` returns 1), so the tuple parser
  must validate each element before calling `make_tuple_type()`.
- Element qualifiers are part of the type:
  `(const int, char)` and `(int, char)` are different types under
  `compare_types(..., unqualified=0)` and the same under
  `unqualified=1`.
- Type identity is structural.  `typedef (int, char) A;` and
  `typedef (int, char) B;` name the same type.
- `t[i]` is the only element accessor.  `i` must be an integer
  constant expression in M1; M5 may add run-time indices for tuples
  whose elements are all laid out contiguously and identically sized
  (a rare case, so probably not worth it).
- `t[i]` is an lvalue when `t` is an lvalue, and a value otherwise,
  following the `.` field-access path (`tccgen.c:7395`-`7418`).
- There is no tuple arithmetic, comparison or cast in M1.  Casting to
  a tuple type is an error until M3's element-wise cast.

`(int, char)` in a *type* position is unambiguous because C has no
parenthesised type-list syntax there.  It must not be confused with:

```c
int (*fp)(int, char);       /* pointer to function, not a tuple */
typedef int F(int, char);   /* function type, not a tuple */
```

Both are parsed by `post_type()` (`tccgen.c:5786`) after the
declarator, not by the type-tuple hook, so they keep their meaning.

### Compile-time value tuples: `$(...)`

`$(...)` builds a value at compile time, so every element must be a
compile-time constant.

```c
enum { N = $(1, 2)[0] };            /* 1 */
static int table[$(4, 8)[1]];       /* 8 elements */
$assert($(4, 8)[1] == 8);

static const int k = $(2 + 3);      /* 5: a plain compile-time value */
static const int m = $(7,)[0];      /* 7: a 1-tuple */

typedef (int, int) Pair;
static Pair p = $(1, 2);            /* M2: constant tuple */
```

Rules:

- `$` is special only immediately before `(`; see *The `$`
  compile-time family*.  `$(` is recognised only in `-lang=d` and
  only when the identifier is exactly `$` and the next token is `(`.
  A function or variable named `$` keeps working in C mode.
- `$(e)` with a single element and no trailing comma is the
  compile-time value of `e`, not a tuple.  `$(e,)` is a 1-tuple, and
  `$(e1, e2, ...)` is a tuple of two or more elements.  This is the
  same arity rule as `(...)` (see D7).
- Elements are parsed with `expr_eq()`, so an element may itself be a
  fixed expression such as a cast, a conditional, or a nested
  `$(...)`.
- Every element must fold to `VT_CONST`, in every context, not only
  inside a constant context (`CONST_WANTED`,
  `tccgen.c:8075`/`8096`).  A run-time value is an error:
  `$`: element 1 is not a compile-time constant.  This is what
  makes `$(...)` a compile-time constructor rather than a spelling
  of `(...)`.
- M2 restricts elements to scalars with a `CValue` representation
  (integers, `_Bool`, enums, `float`, `double`, `long double`, null
  pointers) and to nested constant `$(...)` tuples.  Other aggregates,
  arrays, functions, string literals and address constants are
  rejected until M5.  `$run` (separate task) later widens the set of
  expressions that count as compile-time constant.
- The tuple type is the tuple of the element types, so `$(1, 2.0)`
  has type `(int, double)`, not `(double, double)`.  Element-wise
  conversion happens only when the tuple is assigned to a tuple type
  (M3).
- Empty `$()` is rejected in M2.  `$if` and `$foreach` consume these
  values; they are specified with the reflection task and land in M2
  and M4 respectively (see *Milestones*).

### Run-time value tuples: `(...)`

A run-time tuple literal is written with plain parentheses and is
accepted only where the expected type is already a tuple:

```c
int x = 1, y = 2;

(int, int) t = (x, y);      /* initializer for a tuple-typed object */
t = (y, x);                 /* assignment to a tuple */
f((x, y));                  /* argument for a tuple parameter */
return (x, y);              /* return from a (int, int) function */
((int, char))(i, c);        /* cast operand */
auto (a, b) = f();          /* destructuring initializer */

t[0] = 5;
int z = t[1];
(int, int) q = t;           /* struct copy */
```

Rules:

- `(...)` is a tuple only in a tuple-expected context: an initializer
  for a tuple-typed object, the right side of an assignment to one,
  an argument for a tuple parameter, a `return` from a tuple-returning
  function, a cast operand, or a destructuring initializer.
- Everywhere else `(a, b)` remains a comma expression:

  ```c
  int z = (a, b);               /* comma expression, z = b */
  f((a, b));                    /* one argument, the value b */
  auto t = (a, b);              /* int t = b */
  for (i = 0, j = 0; ...)       /* unaffected: gexpr(), not a paren */
  ```

- The rule is safe because in each tuple-expected context the comma
  expression would be a type error today.  A valid program cannot
  silently change meaning, in C or in D.
- A compile-time `$(...)` tuple is a constant and is accepted in
  every run-time tuple context, e.g. `(int, int) t = $(1, 2);`.
- There is no inferred-type tuple local: `auto t = (a, b);` stays
  `int t = b`.  Name the type (`(int, int) t = (x, y);`) or
  destructure (`auto (a, b) = (x, y);`).  `$(...)` is not a
  workaround, because it requires constant elements.
- Arity matches M2: `(x)` is a parenthesised expression, `(x,)` a
  1-tuple, `(x, y)` a 2-tuple.
- The literal materialises as a struct rvalue.  Assignment,
  initialisation, argument passing and return use the existing
  struct paths (`vstore()` at `tccgen.c:4029`,
  `gfunc_call()`/`gfunc_return()` at `tccgen.c:8131`).
- The ABI is exactly the struct ABI.  On x86-64, a tuple of up to two
  eightbytes is returned in registers via `gfunc_sret()`
  (`x86_64-gen.c:753`); a larger tuple uses the hidden return
  pointer.  No new ABI is introduced.
- `(int, char){a, b}` is the explicit brace-initializer spelling,
  available from M1.
- The value of a tuple is copied, not aliased.  `t[0]` on a
  non-lvalue tuple spills it to a temporary first, like a struct
  member access on an rvalue.

### Rejected spellings

- **`$EXPR`** -- a unary `$` operator needs `$` removed from the
  identifier character set, which re-lexes `$if`, `$x` and `$T`,
  breaks the textual `$assert` match, and collides with opDollar.
  `$(EXPR)` is the same operation with two more characters.
- **`${...}`** -- braces already mean a block, an initializer, a
  statement expression and the `$run { ... }` form.  `${1, 2}` would
  need brace matching and would read like a shell expansion.  `$(...)`
  mirrors the `(...)` run-time spelling and reuses the existing `$`
  sigil.
- **`tuple(...)`** -- reads like a function call and needs a builtin
  declaration or a reserved identifier.  It is a possible fallback
  spelling for C mode, but not the primary one.
- **`#(...)`** -- `#` starts a preprocessor directive; it would need a
  new preprocessor rule.
- **`(...)` everywhere** -- breaks comma expressions in both C and D.

## Comma expressions and parsing hazards

### What is valid today

The obvious fear -- `(T, x)` being a valid C comma expression when
`T` is a typedef name -- is **false**.  A typedef name is not an
expression, so the construct is already a syntax error.  Tested with
gcc 14, clang 19 and this tcc:

```c
typedef int T; int x;
int y = (T, x);      /* gcc/clang/tcc: error: expected ')' before ',' */
int z = (int, x);    /* error */
void f((int) p) {}   /* error */
int (int, char);     /* error */
```

The real hazard is the other direction: `(a, b)` with two *values*
is a valid comma expression, and must stay one.  This is why run-time
tuples use `(...)` only where the expected type is already a tuple,
and why compile-time tuples carry the `$` marker.

### The parser rule

`unary()`'s `case '(':` (`tccgen.c:6678`) already distinguishes a
cast/parenthesised type from an expression by calling
`parse_btype()`:

- If `parse_btype()` succeeds, the group starts with a type.  If the
  next token is `,`, it is a tuple type (new); if it is `)`, it is the
  existing cast/parenthesised type.
- If `parse_btype()` fails, the existing `gexpr()` path parses the
  group as an expression, so `(a, b)` remains a comma expression.

No backtracking is needed in `unary()`, because `parse_btype()`
already commits on the first type token.

In `decl()` (`tccgen.c:11444`), `parse_btype()` is called
speculatively on every statement: a statement beginning with `(T)x;`
is a cast expression, not a declaration.  The type-tuple hook must
therefore be non-committing, and it is a *probe*:

1. Scan the balanced parenthesised token group into two identical
   `TokenString`s without parsing it, tracking `()`, `[]` and `{}`
   nesting so a comma inside a nested construct is not mistaken for a
   tuple separator.  If the first token cannot start a type
   (`is_type_start()`) and is not itself `(`, bail out before
   scanning: every ordinary parenthesised expression stays cheap.
2. If the group has no top-level comma, replay the group into the
   parser unchanged (`begin_macro(str, 1); next();`, the same replay
   mechanism that backs `unget_tok()` at `tccpp.c:3589`) and return
   "not a type".
3. Otherwise replay a copy of the group and parse every top-level
   element as a complete abstract type.  Commit only if all of them
   parse, and then finish the type from the parsed elements.  If any
   element is not a type, unwind the macro stack to its saved top,
   replay the group unchanged and return "not a type".

The probe cannot misread an expression, because a group whose
elements all parse as types can never be a valid expression.  A
"first token can start a type" heuristic alone is not enough: in
`((void)0, (T){0})` (`tests/tests2/94_generic.c:36`) the second
element starts with a type name, yet the group is a comma
expression.  The scan is side-effect free, so a group that turns out
not to be a tuple is restored exactly.  The cost is one token scan
and replay per `parse_btype()` call that sees `(`, which in practice
means only statements that start with a parenthesis.

`for (i = 0, j = 0; ...)` is unaffected because `for` clauses call
`gexpr()` directly (`tccgen.c:9068`, `9083`), not a parenthesised
comma expression.  Function arguments call `expr_eq()`
(`tccgen.c:7591`), so `f((a, b))` stays one argument unless the
parameter type is a tuple.  `return` calls `gexpr()`
(`tccgen.c:8994`), so `return (a, b);` stays a comma expression
unless the function returns a tuple.

Run-time tuple literals don't need the scan/replay hook.  They only
appear inside a parenthesised group that the expression parser is
already reading, so the decision is made in `unary()`'s `case '(':`
(`tccgen.c:6678`): if `parse_btype()` fails and a *tuple context
flag* is set, parse the comma-separated element list as a tuple
literal; otherwise keep the existing comma-expression path.  The flag
is set by the tuple-expected sites (initializer, assignment,
argument, return, cast operand, destructuring) and cleared when
descending into an operand, so `x + (a, b)` never becomes a tuple.

### Compatibility matrix

| construct | meaning today | tuple reading | safe? |
| --- | --- | --- | --- |
| `(int)` | cast/parenthesised type | not a tuple | yes -- stays a cast |
| `(int,)` | error | 1-tuple type | yes |
| `(int, char)` | error | tuple type | yes |
| `(T, U)` typedefs | error | tuple type | yes |
| `(T, x)` typedef + value | error | type tuple with bad element | yes (still an error) |
| `(a, b)` values, non-tuple context | comma expression | comma expression | yes -- unchanged |
| `(a, b)` values, tuple-expected context | comma expression (type error) | run-time tuple | yes -- the context is new |
| `(a)` | parenthesised expression | not a tuple | yes -- stays an expression |
| `f((a, b))`, non-tuple parameter | one argument, value `b` | one argument, value `b` | yes -- unchanged |
| `f((a, b))`, tuple parameter | type error | one tuple argument | yes -- the parameter type is new |
| `int (*fp)(int, char)` | pointer to function | not a tuple | yes |
| `typedef int F(int, char);` | function type | not a tuple | yes |
| `(int, char){1, 2}` | error | tuple compound literal | yes |
| `sizeof((int, char))` | error | tuple size | yes |
| `typeof((int, char))` | error | tuple type | yes |
| `int $x;`, `int $if;` | identifiers | identifiers (unchanged) | yes |
| `$(x)` | call to a function named `$` | compile-time value | **no** -- documented break |
| `$(x)` with a run-time `x` | call to `$` | error (not constant) | yes -- old form was a call |
| `a[$]`, `a[$-1]`, `s[0..$]` | opDollar length | unchanged | yes -- `$` is not before `(` |
| `a[$(i)]` | error ("function pointer expected") | compile-time value used as an index | yes |

### opDollar coexistence

This fork already implements D's opDollar: inside a subscript, `$`
denotes the length of the aggregate (`gen_dollar()`,
`tccgen.c:7144`, guarded by `nb_dollar_ctx`).  The existing test
`tests/tests2/323_d_dollar.c` writes `a[$-1]`, `a[$/2]`, `s[0..$]`
and `vla[$-1]`.

The `$`-before-`(` rule leaves every one of those intact: `$` is
followed by `-`, `/`, `..` or `]`, never by `(`, so it stays the
length expression.  `$(...)` is special only when `(` follows
immediately.

The one interaction to define is `a[$(i)]`: that is the compile-time
value of `i` used as a subscript, so `[`'s integer check applies.  It
cannot silently become `a[$ * (i)]`, because C has no juxtaposition
operator.  `a[$(1)]` is `a[1]`, and `a[$(1, 2)[0]]` is a tuple element
used as an index; both are accepted.

The tuple hook must therefore run *before* the `nb_dollar_ctx`
branch in `unary()` (`tccgen.c:7144`), not after it.

## Language mode decision

Recommendation:

| feature | `-lang=c` | `-lang=d` | reason |
| --- | --- | --- | --- |
| `(T1, T2)` type tuples | yes | yes | previously invalid syntax; no valid C changes meaning |
| `t[i]` tuple access | yes | yes | new type, new operation |
| `(T1, T2){...}` | yes | yes | new type |
| `(...)` run-time literals | yes | yes | read as a tuple only where the expected type is a tuple |
| `$(...)` compile-time tuples | no | yes | `$` is an identifier character; `$(x)` is a call to `$` today |
| `$(e)` compile-time value | no | yes | same `$(` hook as `$(...)` |
| `$if`, `$foreach` | no | yes | contextual `$`-words, like `$assert` |
| `$x`, `$if` as identifiers | yes | yes | a `$`-word is not a keyword |
| `auto (a, b) = ...` | no | yes | new declarator form; D-style |
| tuple `~`/`==` (M5) | no | yes | optional D-style sugar |

Type tuples are a pure extension: enabling them in C mode is
consistent with the fork's policy that compatible C superset
extensions (`defer`, `auto`, slices, `foreach`) are always on
(`README.md:218`).  Run-time tuple literals are safe in C mode for
the same reason: `(...)` is read as a tuple only where the expected
type is a tuple, and such a type can only come from the new syntax.
Compile-time tuples are gated on `lang_d` because `$` is already a
valid identifier character (`tcc.h:811`), and intercepting `$(` would
break a C function named `$`.  `$if` and `$foreach` are D-only for
the same reason; they are contextual, so they do not reserve
`$if`/`$foreach` as identifiers.

Because run-time tuples need no flag, there is no `-ftuples`.  If a
C-mode opt-in for the `$` forms is ever wanted, add a single
`TCCState.tuples` bit next to `lang_d` (`tcc.h:816`) and a
`-ftuples` option in `TCC_OPTION_lang` (`libtcc.c:2080`); do not
scatter `lang_d` checks through the type system.

Fallback: if the M1 parser hook causes a regression in C mode, gate
type tuples on `lang_d` first, land the comma-compatibility tests,
then flip the default in M2.  The representation does not change.

## Internal representation decision

### Option A -- a new `VT_TUPLE` basic type

Rejected.  `VT_BTYPE` is `0x000f` (`tcc.h:1118`) and all 16 values
are used: `VT_VOID` 0 through `VT_AUTO` 15.  The next bit, `0x0010`,
is `VT_UNSIGNED`, so widening `VT_BTYPE` to 5 bits requires
renumbering every type flag and auditing every mask and switch in
`tccgen.c`, all backends, `tccdbg.c`, the initializer code and the
ABI code.  The risk is large and the benefit is only a cleaner name.

The fork has already made this call once: `VT_SLICE` still exists as
value 12, but `make_slice_type()` creates a `VT_STRUCT`, and
`is_slice_type()` tests `type->ref->a.is_slice`.  `VT_SLICE` is now
only read in `type_size()` (`tccgen.c:3741`) and
`find_field_opt()` (`tccgen.c:4504`, `4554`); nothing constructs it.
Tuples should follow the slice path, not the abandoned basic-type
path.

### Option B -- anonymous `VT_STRUCT` + `SymAttr.is_tuple` (recommended)

Add one bit to `struct SymAttr` (`tcc.h:521`) beside `is_slice`
(`tcc.h:533`):

```c
is_tuple : 1;   /**< Struct generated for a tuple type */
```

A tuple type is an anonymous struct symbol:

```c
typedef struct CType { int t; struct Sym *ref; } CType;

/* t == VT_STRUCT, ref points at an anonymous SYM_STRUCT symbol */
```

with positional anonymous fields, one per element, built the same way
as `make_slice_type()` (`tccgen.c:5564`):

```c
static Sym *make_tuple_type(CType *elems, int n)
{
    CType st;
    Sym *s, **ps;
    AttributeDef ad;
    int i;

    st.t = VT_STRUCT;
    st.ref = NULL;
    s = sym_push2(&global_stack, SYM_STRUCT | (anon_sym++), st.t, 0);
    s->r = 0;
    s->type.ref = s;

    ps = &s->next;
    for (i = 0; i < n; ++i) {
        Sym *f = sym_push2(&global_stack, anon_sym++ | SYM_FIELD,
                           elems[i].t, 0);
        f->type.ref = elems[i].ref;
        *ps = f, ps = &f->next;
    }

    memset(&ad, 0, sizeof ad);
    struct_layout(&s->type, &ad);
    s->a.is_tuple = 1;
    s->a.nodebug = 1;   /* M1; see "Debug info" */
    return s;
}
```

Why this works with almost no new code:

- `struct_layout()` (`tccgen.c:4619`) computes size, alignment and
  each field's offset (`f->c = offset`, `tccgen.c:4768`).
- `type_size()` (`tccgen.c:3695`) returns `s->c`/`s->r` for any
  `VT_STRUCT`.
- `vstore()` (`tccgen.c:4029`) already memcpy's struct values.
- `verify_assign_cast()` (`tccgen.c:3929`) already compares struct
  types through `compare_types()`.
- `decl_initializer()` and `init_putv()` (`tccgen.c:9624`) already
  handle struct initializers and target-correct rodata bytes.
- `gfunc_call()`/`gfunc_return()` already implement the struct ABI,
  including `gfunc_sret()` per target.
- `find_field()` (`tccgen.c:4545`) and the `.` postfix
  (`tccgen.c:7395`) already do offset-based field access; tuple
  access is the same operation with a numeric index.

The only new type-system code is `is_tuple_type()`, `make_tuple_type()`,
a structural branch in `compare_types()`, a printer branch in
`type_to_str()`, and a numeric field lookup.

### Option C -- `VT_STRUCT` + a type bit

`tcc.h:1154` still has one unused type bit, `0x00080000`, inside
`VT_TYPE`.  A `VT_TUPLE` flag there would avoid touching `SymAttr`
and make `is_tuple_type()` a one-bit test.  It is a viable fallback,
but it is less robust: `CType.t` is rebuilt in several places
(`type->t = s->type.t` in `struct_decl()` at `tccgen.c:4929`,
`subst_auto_type()` at `tccgen.c:6127`, the typedef branch of
`parse_btype()` at `tccgen.c:5490`), and any path that drops the bit
silently turns a tuple into a plain struct.  A flag on the `Sym` that
*is* the type identity cannot be dropped that way.  It also mirrors
`is_slice`, which maintainers already know.

### Naming and identity

Do **not** generate a struct tag name from the element types, for
example `__tuple_int_char`.  Problems:

- A name must encode qualifiers, pointers, arrays, nested tuples and
  anonymous struct element types.  There is no finite, stable
  spelling for `((int, char) *, struct { int x; })`.
- Two distinct anonymous structs with the same spelling would
  collide, and scoped tags would need scope-qualified names.
- `struct_find()` is keyed by token id; generating tokens for every
  tuple type grows `table_ident` and slows the lexer.
- A generated name is not needed: `type_to_str()` can print
  `(int, char)` on demand, and debug info can synthesise a name at
  emission time.

Identity is structural:

```c
/* in compare_types(), after the s1 == s2 shortcut at tccgen.c:2991 */
if (s1 && s2 && s1->a.is_tuple && s2->a.is_tuple) {
    Sym *f1 = s1->next, *f2 = s2->next;
    for (; f1 && f2; f1 = f1->next, f2 = f2->next)
        if (!compare_types(&f1->type, &f2->type, unqualified))
            return 0;
    return f1 == NULL && f2 == NULL;
}
if (s1 && s2 && (s1->a.is_tuple || s2->a.is_tuple))
    return 0;   /* tuple vs plain struct */
```

This is O(n) per comparison.  If profiling shows repeated comparisons
of the same large tuple types, add a hash-consing table keyed by the
sequence of element `(t, ref)` pairs in `make_tuple_type()`; then most
comparisons hit `s1 == s2` at `tccgen.c:2991` and stay O(1).  Do not
intern by a string name.

### Layout, ABI, initializers, debug

- **Layout:** the target's normal struct layout, including `pragma
  pack` and `__attribute__((packed))` if the tuple type ever acquires
  attributes.  In M1 tuples have no attributes and use the default
  ABI layout.  Document that the layout is *not* specified beyond
  "same as the equivalent struct", so users who need a wire format
  should use an explicit struct.
- **ABI:** identical to the equivalent struct.  This is a feature:
  a tuple can be passed to a function compiled by gcc if the
  equivalent struct type is declared there.  It also means the
  layout is platform-dependent, which is fine for a compiler that
  already exposes struct layout.
- **Initializers:** `Pair p = {1, 'a'};` uses the struct initializer
  path.  A `$(...)` literal is a struct value and is copied with
  `vstore()`.
- **Debug info:** mark tuple structs `a.nodebug = 1` in M1.
  `STRUCT_NODEBUG()` (`tccdbg.c:1747`) then suppresses the anonymous
  struct, avoiding `<anonymous>` field names in DWARF/stabs.  M5 can
  emit a proper tuple type with synthesised field names (`$0`, `$1`,
  ...) if there is demand.

## Compilation cost

The concern is that value tuple literals need "transitive type
inference" of their elements.  The precise answer:

- **Type tuples** `(T1, ..., Tn)` contain *types*, not expressions.
  Each element is parsed by `parse_btype()`/`type_decl()` once.  There
  is no value inference, no constant folding and no code generation.
  Cost: O(n) type parses, n field symbols, one `struct_layout()`
  pass.  Nested tuples cost the size of the type tree, which is
  linear in the source.
- **Compile-time tuples** `$(e1, ..., en)` contain constant
  expressions.  Each `ei` is typed and folded once by the existing
  expression parser; that is the same work as writing the elements as
  separate expressions.  The *new* cost is that all n element values
  and types must be retained at the same time, and the tuple type
  must be built and laid out.  Cost: O(n) `CValue`s, O(n) `CType`
  copies, one `struct_layout()` pass, and one rodata blob of the
  tuple size.
- **Constant folding** happens inside each element parse, so
  `$(1 + 2, 4)` costs no more than `1 + 2` and `4` written
  separately.
- **Comparison:** structural `compare_types()` is O(n) per
  comparison.  Without interning, m comparisons of the same tuple
  type cost O(m*n).  Interning reduces that to O(1) after the first
  construction.  This is the only place where tuple types can become
  superlinear, and only in pathological code.
- **Parser lookahead:** the type-tuple hook scans and replays a
  parenthesised group only when `parse_btype()` is called with `(`
  in front of it.  In `decl()` that means statements beginning with
  `(`; in committed type positions it means a tuple type or invalid
  syntax.  The scan is bounded by the group size, and a group is
  scanned once, not once per element.
- **Code generation:** run-time tuples use the struct copy path, so
  the generated code is the same as for an equivalent struct.  A
  run-time tuple literal with n elements costs n stores into a
  temporary plus the normal copy at the use site.  A compile-time
  tuple costs one rodata blob, which may be dead if the tuple is only
  indexed at compile time; M5 can make the blob lazy.

Summary:

| construct | parse cost | inference | memory | codegen |
| --- | --- | --- | --- | --- |
| `(T1, T2)` | O(n) types | none | n CTypes + n Syms | none |
| `$(c1, c2)` | O(n) exprs + fold | per element, once | n CValues + side table + blob | blob only |
| `(e1, e2)` | O(n) exprs | per element, once | n temporaries + copies | n stores + copy |

A run-time literal must be type-checked against the expected tuple
type, which costs one `compare_types()` per element.  That is the
same order as the struct initializer it replaces.

## Affected components & code details

### 1. Type system and representation (`tcc.h`, `tccgen.c`)

- `struct SymAttr` (`tcc.h:521`): add `is_tuple:1` next to
  `is_slice` (`tcc.h:533`).
- `is_tuple_type()`: mirror `is_slice_type()` (`tccgen.c:3773`):

  ```c
  static int is_tuple_type(CType *type)
  {
      return (type->t & VT_BTYPE) == VT_STRUCT
          && type->ref && type->ref->a.is_tuple;
  }
  ```

- `make_tuple_type()`: new, modelled on `make_slice_type()`
  (`tccgen.c:5564`).  Allocate the struct and field symbols on
  `global_stack` so a tuple type created inside a function does not
  dangle when the scope is popped (a function-scope tuple type can
  escape through `auto` inference or a return type).  `sym_push2()`
  is at `tccgen.c:723`.
- `tuple_arity()` and `tuple_field()`: walk `type->ref->next` and
  return the nth field plus its offset (`f->c`, set by
  `struct_layout()` at `tccgen.c:4768`).  Bound-check the index.
- `compare_types()` (`tccgen.c:2947`): add the structural tuple
  branch after the slice branch (`tccgen.c:2995`).
- `type_to_str()` (`tccgen.c:2730`): in `case VT_STRUCT:`
  (`tccgen.c:2807`), print `(T1, T2)` for tuples before the slice
  branch.  Recursively call `type_to_str()` with a NULL `varstr` for
  each element.
- `type_size()` (`tccgen.c:3695`): no change; the `VT_STRUCT` branch
  already returns `s->c`/`s->r`.

### 2. Parsing type tuples (`tccgen.c`)

- `parse_btype()` (`tccgen.c:5160`): after the initial `type->ref =
  NULL` and `memset(ad, ...)`, if `tok == '('`, call
  `parse_tuple_type_opt()`.  On success, set `type` and return 1 (the
  attributes are already zero).  On failure, the helper restores the
  token stream and `parse_btype()` continues as before.
- `parse_tuple_type_opt()`: scan the balanced group (tracking `()`,
  `[]` and `{}`) into a `TokenString`; if it has no top-level comma
  or its first token cannot start a type, replay and return 0.
  Otherwise parse the tuple type from the replay and return 1.  The
  scan/replay pattern
  is the same one `decl()` uses for `auto` inference
  (`tccgen.c:11549`-`11580`) and `unget_tok()` uses for one token
  (`tccpp.c:3589`).
- `parse_tuple_type_rest()`: called with `tok == ','` after the first
  element has already been parsed by `unary()`'s `case '(':` path.
  Parses the remaining `, T` elements and the closing `)`, then
  builds the type with `make_tuple_type()`.
- `unary()` `case '(':` (`tccgen.c:6678`): after the attribute block
  and `parse_btype()`/`type_decl()`, if `tok == ','`, call
  `parse_tuple_type_rest()`.  The existing compound-literal, cast and
  `TOK_SOTYPE` branches then work unchanged.
- `parse_expr_type()` (`tccgen.c:6306`): `typeof((int, char))` works
  through the `parse_btype()` hook because the tuple's `(` is still
  current when `parse_btype()` is called.
- `parse_type()` (`tccgen.c:6320`): works through the same hook.
- `post_type()` (`tccgen.c:5786`): parameter types starting with `(`
  reach `parse_btype()` at `tccgen.c:5809`, so
  `void f((int, char) p)` works without a change.
- `type_decl()` (`tccgen.c:6134`): no change; pointers, arrays and
  declarator names apply to a tuple type exactly as to a struct.
- `struct_decl()` (`tccgen.c:4879`): field types reach
  `parse_btype()` at `tccgen.c:5017`, so `struct S { (int, char) f; };`
  works without a change.
- `do_d_alias()` (`tccgen.c:10996`): works through the same hook.
- `decl()` (`tccgen.c:11444`): tuple-typed declarations work through
  the hook at `tccgen.c:11480`.  The speculative path must replay the
  group on failure (see *Comma expressions*).

### 3. Parsing value tuples (`tccgen.c`, `tccpp.c`)

Two entry points, one per spelling.

- `$(...)` -- compile-time, constant only.
  - `unary()` `tok_identifier:` (`tccgen.c:7136`): before `sym_find()`
    and the implicit-function fallback, detect
    `lang_d && t >= TOK_UIDENT && !strcmp(get_tok_str(t, NULL), "$")
    && tok == '('`, and call `parse_dollar_tuple()`.  This check must
    come before the `nb_dollar_ctx` branch (`tccgen.c:7144`) so
    `a[$(1, 2)[0]]` is a tuple, while `a[$]` and `a[$-1]` stay
    opDollar.
  - `parse_dollar_tuple()`: consume `(`, parse comma-separated
    `expr_eq()` elements, `)`.  With exactly one element and no
    trailing comma, push the element value unchanged (M2) instead of
    building a tuple type.  Otherwise record each element's `CType`
    and `SValue`; require `VT_CONST` for every element (in every
    context), store the `CValue`s in the side table, emit a rodata
    blob and push a `VT_CONST|VT_SYM` reference.
  - A non-constant element is diagnosed at the element: `$`: element
    %d is not a compile-time constant.  There is no fallback to a
    run-time tuple; the spelling is the contract.
- `(...)` -- run-time, context-directed.
  - Add a `CType *tuple_expect` (or a small depth-counted flag) set
    by the tuple-expected sites: `decl_initializer_alloc()`
    (`tccgen.c:10086`) and `decl_initializer()`, `gen_assign_cast()`
    (`tccgen.c:4021`), `gfunc_param_typed()` (`tccgen.c:6263`),
    `gfunc_return()`'s caller (`tccgen.c:8131`), the cast path in
    `unary()` (`tccgen.c:6727`), and the destructuring path in
    `decl()` (M4).
  - In `unary()`'s `case '(':` (`tccgen.c:6678`), after
    `parse_btype()` fails: if the flag is set, parse the
    comma-separated element list with `expr_eq()` into a tuple value
    of the expected type; otherwise take the existing
    parenthesised-expression path.
  - Clear the flag on entry to `unary()` before descending into
    operands, so only the outermost parenthesised group of an
    expected expression is eligible.  Restore it on return.  This is
    what keeps `x + (a, b)`, `f((a, b), c)` and nested comma
    expressions unchanged.
  - Type-check the elements against the expected tuple type and
    convert each with `gen_assign_cast()`; then build a struct
    temporary exactly as an equivalent `{...}` initializer would.
- `$if`/`$foreach` (M2/M4): in `block()` (`tccgen.c:8869`) right
  after `t = tok` (8879), if `t` is the identifier `$if` or
  `$foreach` and `tok == '('`, rewrite `t` to `TOK_IF`/`TOK_FOREACH`
  and fall into the existing statement paths (8889, 9058).  `$if`
  evaluates its condition with `expr_const()` and emits only the
  selected branch; `$foreach` iterates a constant tuple and replays
  its body once per element.  Both are shared with the reflection
  task; the token-replay machinery they need is the same one
  `tasks/compile_time_function_execution.md` specifies for `$run`.
- `tccpp.c` needs no change: `$` is already an identifier character
  (`tccpp.c:2696`, `3710`), and `$(` tokenises as the identifier `$`
  followed by `(`.
- `tcctok.h` needs no new keyword; keeping `$` contextual is what
  preserves C-mode compatibility.

### 4. Element access (`tccgen.c`)

- `[` postfix (`tccgen.c:7421`): add a tuple branch before the
  array/slice/pointer branch:

  ```c
  if (is_tuple_type(&base_type)) {
      int idx = expr_const();
      Sym *f = tuple_field(&base_type, idx, &cumofs);
      /* constant tuple? look up the side table and push the value */
      /* otherwise reuse the field-address sequence below */
  }
  ```

- Reuse the `.` postfix sequence at `tccgen.c:7401`-`7418`:
  `gaddrof()`, add `cumofs`, set `vtop->type = f->type`, set
  `VT_LVAL` (unless the field is an array), and keep the `-b`
  `VT_MUSTBOUND` handling.
- For a compile-time tuple constant, the side table returns the
  element `CValue` and type directly, so `$(1, 2)[0]` works in
  `enum`, array bounds, `static assert` and `$assert` without
  codegen.
- `.sizeof` on a tuple already works through the D `.sizeof` path at
  `tccgen.c:7214`; no change.

### 5. Code generation (`tccgen.c`, backends)

- `vstore()` (`tccgen.c:4029`): no change; the `VT_STRUCT` branch
  memcpy's tuple values.
- `verify_assign_cast()` (`tccgen.c:3929`): no change; the
  `VT_STRUCT` branch uses `is_compatible_unqualified_types()`, which
  becomes structural for tuples through `compare_types()`.
- `gen_cast()` (`tccgen.c:3456`): M1 must reject casts involving a
  tuple type explicitly.  `gen_cast()` alone does not catch a
  struct-to-struct cast: `sbt == dbt == VT_STRUCT`, so the
  `sbt != dbt` block at `tccgen.c:3478` is skipped and the cast is
  silently accepted (current tcc accepts `(struct A)b` for unrelated
  `A` and `B`).  Add the check in the cast path of `unary()`
  (`tccgen.c:6727`); do not put an unconditional check at the top of
  `gen_cast()`, because `gen_assign_cast()` calls `gen_cast()` for
  every assignment, including the tuple assignments M3 introduces.
  M3 adds `gen_tuple_cast()` in `gen_assign_cast()`
  (`tccgen.c:4021`) and in the same cast path to convert element-wise
  before the struct copy.
- `gfunc_param_typed()` (`tccgen.c:6263`): no change; it calls
  `gen_assign_cast()`.
- `gfunc_return()` (`tccgen.c:8131`): no change; the `VT_STRUCT`
  branch handles tuple returns, including the hidden return pointer.
- Backends: no change.  `gfunc_sret()` per target already classifies
  struct returns; tuples are structs.

### 6. Constant tuples and the side table (`tccgen.c`)

A `$(...)` constant must be usable both as a value and as a source of
compile-time element values.  Keep a per-translation-unit table:

```c
struct TupleConst {
    Sym *sym;        /* blob symbol from get_sym_ref() */
    int n;           /* arity */
    CType *types;    /* element types */
    CValue *vals;    /* scalar element values, for constant indexing */
    struct TupleConst **nested; /* tuple-valued elements, or NULL */
};

static TCC_TLS struct TupleConst *tuple_consts;
static TCC_TLS int nb_tuple_consts;
```

- Key the table by the `Sym *` returned by `get_sym_ref()`
  (`tccgen.c:1193`) or `vpush_ref()` (`tccgen.c:1206`).  Allocate
  those symbols on `global_stack` so a later `sym_pop()` cannot free
  the key and let a fresh symbol reuse its address.
- Reset the table per translation unit at the start of
  `tccgen_compile()` next to the `anon_sym` reset (`tccgen.c:450`);
  free the previous table's storage there before clearing it.
- Emit the blob with `init_putv()` (`tccgen.c:9624`) at each field
  offset rather than memcpy'ing `CValue` bytes, so target byte order,
  relocations and `long double` representation are correct when
  cross-compiling.
- M2 records only elements that are plain `VT_CONST` (no `VT_SYM`).
  A string literal or address constant is therefore rejected as a
  `$(...)` element in M2, with the "not a compile-time constant"
  diagnostic from *Compile-time value tuples*; the side table cannot
  represent the relocation it would need.  M5 extends the table with
  a symbol and addend per element.
- A nested constant tuple is recorded as a nested `TupleConst`.
  The outer blob copies the nested tuple's bytes at the field
  offset, and the side table keeps the nested entry so constant
  indexing can descend into it.
- Do not replay the element tokens on each index.  Replaying would
  re-evaluate side effects and cost O(n) per index; the table makes
  indexing O(1).

### 7. Destructuring (M4, `tccgen.c`)

- `decl()` after `parse_btype()` (`tccgen.c:11480`): if the type has
  `VT_AUTO` (`has_auto_type()`, `tccgen.c:6113`) and `tok == '('`,
  parse a tuple pattern `(name1, name2, ...)` instead of a normal
  declarator.
- Save and replay the initializer exactly as the existing `auto`
  path does (`tccgen.c:11544`-`11580`): `expr_type()` deduces the
  initializer type, then `subst_auto_type()` would install it.  For a
  tuple pattern, require the deduced type to be a tuple of the same
  arity, allocate one local per name with the element type, and store
  each field.
- If the initializer is a compile-time tuple constant, the fields are
  available from the side table, so the names can be bound as
  manifest constants instead of locals (M5; useful for reflection).
- The initializer must be evaluated exactly once.  Reuse the
  `init_str` save/replay so side effects are not duplicated.

### 8. Diagnostics, dumps, debug info (`tccgen.c`, `tccdbg.c`)

- `type_to_str()` (`tccgen.c:2730`): print `(int, char)`; use it in
  `cast_error()`, `type_incompatibility_error()` and `-fdump-decls`.
- Index diagnostics: `"tuple index %d out of range for type
  '(int, char)'"`, `"tuple index must be a constant expression"`,
  `"cannot cast to tuple type"`, `"tuple element %d has incomplete
  type"`.
- `tccdbg.c`: mark tuple structs `nodebug` in M1
  (`STRUCT_NODEBUG()`, `tccdbg.c:1747`).  M5 can emit DWARF as an
  anonymous struct with fields `$0`, `$1`, ... or a
  `DW_TAG_structure_type` named `(int, char)` if debuggers accept it.
- `-fdump-decls`/`psyms()`: tuple fields are anonymous
  (`anon_sym++ | SYM_FIELD`), so they are not linked into
  `table_ident`; check that the dump does not print meaningless
  field names.

### 9. Options, docs, tracker (`libtcc.c`, `README.md`, `TODO.md`)

- `-lang=d` sets `lang_d` in `TCC_OPTION_lang` (`libtcc.c:2080`);
  `tdc` does the same (`tcc.c:347`).  Only the `$` forms need it;
  type tuples and `(...)` run-time literals work in both modes.
- No `-ftuples` flag: run-time tuples are enabled by the context
  rule, not by a mode bit.  A future opt-in for the `$` forms would
  add a `TCCState.tuples` bit next to `lang_d` (`tcc.h:816`).
- `README.md`: add a "Tuples" subsection under "D-style language
  extensions" (`README.md:245`), after slices and before aliases, and
  a sentence in "Language modes" (`README.md:201`) explaining the
  three spellings: `(T1, T2)` types, `$(...)` compile time, `(...)`
  run time.
- `TODO.md`: cross-reference the reflection proposal
  (`TODO.md:151`-188) and note that `$fields`/`$members` return
  compile-time tuples.

## Function-by-function change list

| file | symbol | change |
| --- | --- | --- |
| `tcc.h` | `struct SymAttr` (521) | add `is_tuple:1` |
| `tccgen.c` | new | `is_tuple_type()`, `tuple_field()`, `tuple_begin()`, `tuple_add_field()`, `tuple_layout()`, `tuple_field_name()` |
| `tccgen.c` | new | `parse_tuple_type_opt()`, `parse_tuple_type_rest()`, `parse_tuple_group()`, `scan_paren_group()`, `gen_tuple_field()`, `check_tuple_element()` |
| `tccgen.c` | `compare_types()` (2947) | structural tuple branch |
| `tccgen.c` | `type_to_str()` (2730) | print `(T1, T2)` |
| `tccgen.c` | `parse_btype()` (5160) | tuple hook in the specifier loop |
| `tccgen.c` | `unary()` `case '(':` (6678) | continue a tuple type after the first element; M3: run-time tuple literal |
| `tccgen.c` | `unary()` `tok_identifier:` (7136) | intercept `$(` in `-lang=d` |
| `tccgen.c` | `gen_dollar()` (7144) | no change; the `$(` hook must run before it |
| `tccgen.c` | `block()` (8869) | M2/M4: `$if` and `$foreach` statement forms |
| `tccgen.c` | `[` postfix (7421) | tuple indexing and constant lookup |
| `tccgen.c` | `unary()` cast path (6727) | M1: reject tuple casts; M3: `gen_tuple_cast()` |
| `tccgen.c` | `gen_assign_cast()` (4021) | M3: `gen_tuple_cast()` |
| `tccgen.c` | `decl()` (11444) | M4: `auto (a, b) = ...` pattern |
| `tccgen.c` | `tccgen_compile()` (445) | reset the tuple constant table |
| `tccgen.c` | `init_putv()` (9624) | reuse for tuple blobs; no signature change |
| `tccdbg.c` | `STRUCT_NODEBUG()` (1747) | tuples marked `nodebug` in M1 |
| `tccpp.c` | `next()` (2696) | no change; `$` already an identifier char |
| `README.md` | D extensions (245) | document the three spellings |
| `TODO.md` | reflection (151) | cross-reference tuple support |

## Milestones

**M0 -- decisions and scaffolding.**  This document; the
`SymAttr.is_tuple` bit; `is_tuple_type()`, `tuple_begin()`,
`tuple_add_field()`, `tuple_layout()`, `tuple_field()`; the
`compare_types()` and `type_to_str()` branches.  No user-visible
syntax yet; the code is exercised by M1.  Exit criterion: `make
-j$(nproc) && make -j$(nproc) test` passes with no behaviour change.
Status: implemented; it ships together with M1 (see D8).

**M1 -- type tuples (P1).**  Parse `(T1, ..., Tn)` in declarations,
typedefs, `alias`, fields, parameters and return types; `(T,)` for
1-tuples; `()` rejected.  `sizeof`, `_Alignof`, `typeof`, `_Generic`,
compound literals, pointers/arrays of tuples, `t[i]` with a constant
index, and `__builtin_types_compatible_p`.  Structural identity.
Tuple ABI = struct ABI.  Debug info suppressed.  Type tuples enabled
in C and D modes.  Exit criteria: `421`-`427` pass, including the
comma-expression compatibility tests and a cross-mode build.
Status: implemented (see D8).

**M2 -- compile-time value tuples (P2).**  `$(...)` in `-lang=d`,
compile-time constants only; `$(e)` is a value and `$(e,)` a 1-tuple;
a non-constant element is an error everywhere, not only in a constant
context; side table; rodata blob via `init_putv()`; constant indexing
in `enum`, array bounds, `static assert`, `$assert`; nested `$(...)`;
empty `$()` rejected; `$if (c) ...` over a compile-time condition.
Exit criteria: `428`-`429` pass; a compile-time `$(...)` produces no
code beyond the rodata blob; `-lang=c` still parses `$(x)` as a call
to `$`.

**M3 -- run-time value tuples (P3).**  `(...)` as a tuple literal in
the six tuple-expected contexts, in both modes; the tuple-context
flag and its clearing rules; tuple temporaries; assignment, copy,
arguments and returns; element-wise conversion on assignment and
casts; `t[i]` on rvalues.  No inferred-type tuple local:
`auto t = (a, b);` stays `int t = b`.  Exit criteria: `430`-`434`
pass; the comma-expression suite passes in both modes; the ABI
matches the equivalent struct for 8-, 16- and >16-byte tuples.

**M4 -- destructuring and multiple returns (P3 ergonomics).**
`auto (a, b) = expr;`; evaluation exactly once; arity and type
checks; destructuring a tuple-typed variable and a function result;
`$foreach (x; t) ...` over a constant tuple.  Exit criteria:
`435`-`436` pass; the initializer's side effects occur once.

**M5 -- type-level and constant-level operations (P2/P3).**
Hash-consing/interning if profiling justifies it; `typeof(t[i])`;
compile-time tuple concatenation with `~` (reusing the slice `~`
operator at `tccgen.c:7852` if it fits); element-wise `==`/`!=`
(optional); struct and array elements in compile-time tuples;
manifest-constant destructuring; proper debug info; `-fdump-decls`
polish.  Exit criteria: `437`-`438` pass; `make pgo PGO_CC=clang`
shows no regression.

The reflection task in `TODO.md:151` can then build
`$fields(T)`/`$members(T)` and `$for` on top of `$foreach` (M4);
that work is not part of this task.  A `$tupleof(T)` that returns
the member types of a struct (D's `T.tupleof`, see open question 15)
would sit alongside them, as the type-level projection of
`$fields(T)`.

## Test plan

New tests go in `tests/tests2/`, starting at the next free prefix
(`407`-`420` were taken by other work while this was written, so M1
uses `421`-`427`), following the conventions in that `Makefile`
(`FLAGS += -lang=d` for D tests, `GEN = $(GEN-TCC)` where the test
depends on tcc-specific behaviour, `-dt` for multi-snippet error
tests, `-b` for bounds checking).  The multi-file ABI test follows
`120_alias.c`: the extra translation unit is named `425+_tuple_abi.c`
and pulled in with `FLAGS += $(subst 425,425+,$1)`.

| file | milestone | asserts |
| --- | --- | --- |
| `421_tuple_type.c` | M1 | typedef, declaration, nested tuple, `sizeof`/`_Alignof`, compound literal, field access, `typeof`, `_Generic`, `__builtin_types_compatible_p` |
| `422_tuple_type_error.c` (`-dt`) | M1 | `()`, `(void, int)`, function element, incomplete element, out-of-range index, non-constant index, cast to tuple, tuple arithmetic |
| `423_tuple_comma_compat.c` | M1 | `(a, b)`, `f((a, b))`, `for (i = 0, j = 0; ...)`, `return (a, b);`, `(T)x` casts; all unchanged in C mode |
| `424_d_tuple_comma_compat.c` (`-lang=d`) | M1 | the same comma expressions in D mode, plus `a[$]`, `a[$-1]`, `s[0..$]` and `$assert` |
| `425_tuple_abi.c` | M1 | pass/return tuples of 8, 16 and >16 bytes, mixed int/float, nested tuples; compare against the equivalent struct |
| `426_tuple_abi_bcheck.c` (`-b`) | M1 | tuple field access under bounds checking |
| `428_d_tuple_value.c` (`-lang=d`) | M2 | `$(...)` constants in `enum`, array bounds, `static assert`, `$assert`; `$(e)` as a value and `$(e,)` as a 1-tuple; nested `$(...)`; `$if` over a constant condition; mixed element types |
| `429_d_tuple_value_error.c` (`-dt -lang=d`) | M2 | non-constant element (in any context), struct-valued element, empty `$()`, `$(e)` where a tuple is required, out-of-range index, `$` without `(`, function named `$`, non-constant `$if` condition |
| `430_tuple_runtime.c` | M3 | `(x, y)` in initializer, assignment, argument, return, cast and destructuring contexts; copy/assign; element-wise conversion |
| `431_tuple_runtime_error.c` (`-dt`) | M3 | arity/type mismatch in a tuple literal, tuple arithmetic, indexing a non-tuple, non-tuple parameter with `(...)` |
| `432_tuple_paren_compat.c` | M3 | `(a, b)` remains a comma expression everywhere else; `auto t = (a, b)` is an int; `x + (a, b)` is an int |
| `433_d_tuple_paren_compat_d.c` (`-lang=d`) | M3 | the same comma expressions in D mode, plus `a[$(1)]` and `a[$]` next to each other |
| `434_tuple_c_mode.c` | M3 | type tuples and `(...)` run-time literals in C mode; `$(...)` still calls a function named `$`; `$x` and `$if` remain identifiers |
| `435_d_tuple_destructure.c` (`-lang=d`) | M4 | `auto (a, b) = f();`, destructuring a variable and a compile-time tuple, evaluation once, `$foreach` over a compile-time tuple |
| `436_d_tuple_destructure_error.c` (`-dt -lang=d`) | M4 | arity mismatch, non-tuple initializer, duplicate names |
| `437_d_tuple_type_ops.c` (`-lang=d`) | M5 | `typeof(t[i])`, tuple `~`/`==` if implemented, qualifiers, interning identity |
| `438_d_tuple_manifest_destructure.c` (`-lang=d`) | M5 | destructuring a compile-time tuple into manifest constants; no side effects at the binding site |

Additional coverage:

- A `tests/libtcc_test.c` case that compiles a string containing a
  tuple through the library API, to prove the feature does not
  depend on the `tcc` driver.
- The full `./configure && make -j$(nproc) -B && make -j$(nproc)
  test` sequence for every milestone that touches the compiler.
- `make pgo PGO_CC=clang` after M1, M2, M3 and M5; record the
  Mlines/s and Valgrind `I refs` against `bench/pgo-history.tsv`.
- A `-lang=c` build of a source that defines and calls a function
  named `$`, to prove the C-mode `$` behaviour is unchanged.

## Risks and limitations

- **Parser regression in `decl()`.**  `parse_btype()` is called
  speculatively on every statement.  The scan/replay hook must leave
  the token stream exactly as it found it on the "not a tuple" path.
  Mitigation: the `409`/`410` comma-compatibility tests, plus a
  debug build with `-fdump-decls` on the existing tests.
- **Context-flag leakage.**  The run-time tuple flag must be cleared
  when descending into a subexpression and restored afterwards.
  Otherwise `x + (a, b)`, `f((a, b), c)` or a comma expression inside
  a tuple element could be read as a tuple.  Mitigation: set the flag
  at the six sites only, clear it at the top of `unary()`, and cover
  each nesting case in `417`/`418`.
- **Run-time tuples in C mode.**  They rely on the expected type
  being a tuple, which only new code can produce.  If the context
  plumbing turns out to be imprecise at any site, gate the flag on
  `lang_d` as a fallback; the type system and ABI do not change.
- **`$(...)` call compatibility.**  In `-lang=d`, a function named
  `$` can no longer be called as `$(...)`; it is parsed as a
  compile-time value or tuple.  This is an intentional, documented
  break.  C mode is unaffected.  If the break ever matters, preserve
  `$ (x)` (with a space) by checking the source character before `(`
  when `$` is matched; the tokenizer still knows the position.
- **`$`-word capture.**  `$if` and `$foreach` are contextual, so a
  variable named `$if` immediately followed by `(` is a static `if`.
  This is the same trade-off as `$assert`; keep the set of `$`-words
  small and document each one.
- **Structural vs. nominal types.**  A tuple must never become
  compatible with a plain struct that happens to have the same
  fields.  `compare_types()` checks `is_tuple` on both sides and
  returns 0 for a tuple/struct pair.  Add a test for
  `__builtin_types_compatible_p((int, char), struct { int a; char b; })`
  returning 0.
- **Type symbol lifetime.**  A tuple type created inside a function
  can escape through `auto` inference or a return type.  Allocate
  tuple type symbols and tuple constant blob symbols on
  `global_stack`, not on the function's `local_stack`; otherwise
  `sym_pop()` can free the type while a `CType` still points at it.
- **Side-table key reuse.**  If a blob symbol is popped and a new
  symbol reuses its address, a stale table entry could match.  The
  `global_stack` allocation above prevents this; free the table at
  TU end.
- **Cross-compilation.**  Constant tuple blobs must use the target's
  byte order and representation.  Emit them with `init_putv()`
  (`tccgen.c:9624`), not with a raw `CValue` memcpy.
- **`long double` and 128-bit elements.**  `CValue` has `ld` and
  `tab[]`; `VT_QLONG`/`VT_QFLOAT` are ABI-only and should not appear
  as source-level tuple elements.  Test `long double` layout against
  the equivalent struct on i386 and x86-64.
- **ABI is platform-dependent.**  Tuples are not a wire format.  A
  tuple passed across a gcc/tcc boundary only works if the equivalent
  struct is declared identically.  Document this in the README.
- **Debug info.**  M1 suppresses tuple debug info.  A user debugging
  a tuple-typed variable sees no type or an anonymous struct.  M5
  must emit a usable type before tuples are recommended for general
  use.
- **Code size.**  A compile-time tuple always emits a rodata blob,
  even if it is only indexed at compile time.  M5 can make the blob
  lazy; until then, keep `$(...)` constants out of hot code or accept
  the few dead bytes.
- **`-b` bounds checking.**  Tuple field access goes through
  `gaddrof()` and offset arithmetic; reuse the `.` postfix's
  `VT_MUSTBOUND` handling (`tccgen.c:7413`) and test with `-b`.
- **Comma-expression surprises.**  `auto t = (a, b);` stays an
  `int`, and `(a, b)` is a tuple only where a tuple is expected.
  Document it prominently; this is the price of not breaking comma
  expressions.

## Open questions

1. Should `()` be the unit type (size 0) or an error?  Recommendation:
   error in M1; unit type later only if reflection needs an empty
   tuple.
2. Should `(T,)` be a 1-tuple, or should 1-tuples be rejected
   entirely?  Recommendation: accept `(T,)`, `$(e,)` and `(e,)`;
   `(T)`, `$(e)` and `(e)` stay a parenthesised type or value, as in
   Python and Swift.
3. Should tuple types be nominal when typedef'd, or structural?
   Recommendation: structural, like slices.
4. Is `t[i]` the right accessor, or should it be `t.0`/`t.$0`?
   Recommendation: `t[i]`, D-style; `t.0` is not a lexable
   identifier and `t.$0` adds another `$` form.
5. Should tuples support element-wise `==`/`!=`?  Recommendation:
   M5, optional; structs currently reject comparison, so this is a
   new semantic rather than a natural extension.
6. Should `~` concatenate tuples?  Recommendation: M5 for
   compile-time tuples, reusing the slice `~` operator; run-time
   concatenation is just a new tuple literal.
7. Should the `$` forms be allowed in C mode behind `-ftuples`?
   Recommendation: no by default; run-time tuples already work in C
   mode via `(...)`, so the flag would only enable compile-time
   tuples, which is a rare need.
8. Should destructuring support explicit element types, e.g.
   `auto (int a, char b) = expr;`?  Recommendation: M4/M5; start
   with `auto` only.
9. Should `$(...)` be an lvalue, like a compound literal?  A
   compound literal `(T){...}` is an lvalue in C; `$(...)` should be
   an rvalue (a value), and `(T){...}` is the lvalue spelling.
10. Is hash-consing worth it from M1, or only after profiling?
    Recommendation: structural compare in M1; intern in M5 if the
    PGO/cachegrind numbers justify it.
11. What should `type_to_str()` print for a tuple containing an
    anonymous struct?  Recommendation: `(int, struct <anonymous>)`,
    matching the existing anonymous-struct spelling at
    `tccgen.c:2824`.
12. Should `$if`/`$foreach` be keywords in `-lang=d` instead of
    contextual `$`-words?  Recommendation: contextual, matching
    `$assert`; a keyword would reserve `$if`/`$foreach` as
    identifiers for no gain.
13. Should a bare `$EXPR` operator be added later?  Recommendation:
    no; see *Why bare `$EXPR` is not adopted*.
14. Should `$T`/`$param` be the template-parameter spelling?
    Recommendation: yes as a naming convention, but templates are a
    separate task and must not constrain the tuple grammar.
15. Should there be a `$tupleof` counterpart of D's `T.tupleof`?
    D returns the types of a struct's members as a tuple; the same
    is convenient here for a struct type `T` (and for a tuple type,
    where it is the identity).  Recommendation: yes, later, behind a
    `$`-word rather than a `.tupleof` property: `$tupleof(T)` keeps
    the spelling out of the D namespace, where `T.tupleof` would
    also read as a field access on a type.

    How it relates to `$fields(T)` (`TODO.md:158`): they are two
    projections of one member list, one type-level and one
    value-level.

    - `$tupleof(T)` is a *type tuple*: the member types in
      declaration order.  It is usable wherever a type is
      (`$tupleof(S) x;`, a parameter list, a typedef), needs no side
      table and no rodata, and is the one to implement first --
      it is M1 machinery applied to a struct's member list.
    - `$fields(T)` is a *compile-time value tuple* of reflection
      nodes, each with `.name`, `.type`, `.offset`, `.size`,
      `.is_pointer`, ...  It needs M2 (value tuples, a node type,
      rodata) and M4's `$foreach` to be useful.
    - They must agree exactly: same arity and declaration order, so
      that `$fields(T)[i].type` is `$tupleof(T)[i]` and generic code
      can walk `$foreach (f; $fields(T))` and index either form by
      the same `i`.  `$fields` is `$tupleof` plus per-member
      metadata, not a different list.  `$members` is that list
      widened to methods and nested types, so only its field
      entries project to `$tupleof`.
    - Anonymous aggregates are flattened in both, because their
      members are members of the enclosing struct.

## Alternatives considered

- **A new `VT_TUPLE` basic type.**  Rejected: `VT_BTYPE` is full,
  widening it renumbers `VT_UNSIGNED` and every type flag, and every
  backend switch would need an audit.  The slice implementation
  already abandoned `VT_SLICE` for an anonymous struct.
- **A struct with a generated tag name.**  Rejected: names cannot
  encode qualifiers, nested tuples and anonymous struct elements
  without collisions; `table_ident` would grow; `struct_find()` is
  the wrong lookup for a structural type.
- **A library `Tuple` type.**  Rejected for P1/P2: a library type
  cannot provide compile-time type tuples, destructuring or
  reflection without templates.  It remains a reasonable user-level
  alternative for P3 if the built-in type is not enough.
- **`(...)` for value tuples everywhere.**  Rejected: it silently
  changes `(a, b)` from a comma expression to a tuple in valid C and
  D code.  The context-directed rule gets the syntax without the
  breakage.
- **One spelling for both kinds (`$(...)` with run-time elements).**
  Rejected: a literal whose meaning depends on whether its elements
  happen to be constant is hard to read, makes compile-time
  evaluation implicit, and would need a silent fallback from
  constant to run-time construction.  Separate spellings make the
  intent explicit and keep `$(...)` usable as a compile-time
  diagnostic.
- **`$run`-style prefix with a bare expression (`$EXPR`).**  Rejected
  for now: `$` is an identifier character, so `$x` is a single token
  today; the bare form needs a dedicated lexer mode, re-lexes every
  `$`-prefixed identifier, breaks the textual `$assert` match, and
  collides with opDollar.  `$(EXPR)` gives the same meaning.
- **`$` as a `tcctok.h` keyword.**  Rejected: it would reserve every
  `$`-prefixed identifier and change valid programs.  Contextual
  matching is what `$assert` already does.
- **`${...}`.**  Rejected: braces already mean blocks, initializers
  and `$run { ... }`; `$(...)` is unambiguous and mirrors `(...)`.
- **`tuple(...)` builtin.**  Rejected as the primary spelling: it
  reads like a function call and needs a reserved identifier.  It is
  a possible C-mode fallback.
- **Token replay instead of a constant side table.**  Rejected:
  `$(e1, e2)[0]` would re-evaluate the elements on every index and
  cost O(n) per access.  The side table makes indexing O(1) and
  preserves side-effect semantics.
- **A separate tuple ABI.**  Rejected: reusing the struct ABI means
  no backend changes, and tuples interoperate with equivalent
  structs.  The cost is platform-dependent layout, which is already
  true for structs.

## Acceptance criteria

- **P1:** `-lang=c` and `-lang=d` accept `typedef (int, char) Pair;`,
  `Pair p = {1, 'a'};`, `p[0]`, nested tuples, tuple parameters and
  returns, `sizeof`/`_Alignof`/`typeof`/`_Generic`, compound literals,
  and `__builtin_types_compatible_p((int, char), (int, char))`.
  A tuple is not compatible with a plain struct of the same layout.
- **P2:** `-lang=d` accepts `$(...)` constants in `enum`, array
  bounds, `static assert` and `$assert`; `$(1, 2)[0]` folds at
  compile time and emits only the rodata blob; `$(e)` is a value and
  `$(e,)` a 1-tuple; `$if` works on a constant condition;
  `-lang=c` still treats `$(x)` as a call to `$`.
- **P3:** `(...)` builds a run-time tuple in initializer,
  assignment, argument, return, cast and destructuring contexts, in
  C and in D; tuple assignment/copy, tuple arguments and returns, and
  `auto (a, b) = expr;` work; the ABI matches the equivalent struct
  for 8-, 16- and >16-byte tuples.
- **Separate spellings:** `$(x, y)` requires constant elements and
  is an error when one is not constant; `(x, y)` is a tuple only in
  a tuple-expected context and a comma expression everywhere else;
  neither spelling is accepted where the other is required.
- **Comma compatibility:** every valid C and D comma expression in
  `409`/`410`/`417`/`418` keeps its meaning; `auto t = (a, b);` is an
  `int`; `f((a, b))` is one argument; `x + (a, b)` is an int.
- **Modes:** type tuples and `(...)` run-time literals work in C and
  D; only the `$` forms are `-lang=d` only.
- **`$` family:** `$(e)` is a value, `$(e,)` a 1-tuple, `$(e1, e2)`
  a tuple; `$if`/`$foreach` are contextual `$`-words, not keywords;
  `int $x;`, `int $if;` and a function named `$` still parse; opDollar
  (`a[$]`, `a[$-1]`, `s[0..$]`) is unchanged.
- **Robustness:** out-of-range indices, non-constant indices,
  incompatible assignments, `void`/function/VLA/incomplete elements
  and casts to tuples produce actionable diagnostics, not compiler
  crashes.
- **Verification:** `./configure && make -j$(nproc) -B && make
  -j$(nproc) test` passes; `make pgo PGO_CC=clang` shows no
  regression in Mlines/s or Valgrind `I refs` against
  `bench/pgo-history.tsv`.
