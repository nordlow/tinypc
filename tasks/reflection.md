# Task: Compile-Time Reflection (`$fields`, `$members`, `$tupleof`)

## Status

Follow-up to `tasks/support_tuples.md` (M1-M5 complete).  The tuple
substrate (`$(...)`, `(...)`, destructuring, `$foreach`, `$if`) is the
foundation; reflection builds on it.

### Deferred items from M5 (not yet implemented)

- **B -- tuple `~` concatenation and element-wise `==`/`!=`** (optional
  D-style sugar).  Reuse the slice `~` operator if it fits.
- **C -- full `$fields(T)`/`$members(T)` reflection nodes** with
  `.name`, `.type`, `.offset`, `.size`, `.is_pointer`, plus UFCS
  `T.$fields`, RTTI tables, and `$for`/`$while`.

## First step: `$tupleof(T)`

`$tupleof(T)` evaluates at compile time to a tuple type whose elements
are the (unqualified) types of the members of `T`, in declaration
order.  It is the type-level projection of `$fields(T)` and exercises
the tuple substrate without needing reflection nodes or names.

Syntax:

    typedef $tupleof(S) ST;          /* (int, double) for struct S { int a; double b; } */
    $foreach (T; $tupleof(S)) { ... }   /* T is a type? requires type-valued foreach */

Open questions: `$foreach` currently binds values, not types.  A type
value needs a representation; the first milestone may restrict
`$tupleof` to `typeof`-style use (`typeof($tupleof(S)[0])`) and
manifest type aliases rather than value-level iteration.

## Milestones

- **R1:** `$tupleof(T)` as a type-level expression usable in `typedef`,
  `sizeof`, and `typeof`.  Exit: new test `tests/tests2/44?_d_tupleof.c`.
- **R2:** `$fields(T)` returning a constant tuple of field names (strings)
  and types.  Exit: destructuring/foreach over `$fields(S)`.
- **R3:** `$members(T)`, RTTI tables, UFCS syntax.
- **R4:** `$for`/`$while` control flow over reflection nodes.

## Related

- `TODO.md` "Proposal: Compile-Time & Run-Time Reflection".
- `tasks/support_tuples.md` sections 5-8.
EOF
echo done