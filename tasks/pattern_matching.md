# Task: Pattern Matching (Dependent on Tuple Support)

> **Status:** Proposed. Prerequisite: `tasks/support_tuples.md`.

## Goal

Implement structural pattern matching (`match` expression/statement) in
TinyPC (TPC), leveraging the tuple support defined in
`tasks/support_tuples.md` for destructuring multi-value expressions,
tuples, structs, and tagged unions.

## Syntax & semantics

### Basic form

```c
match (expr) {
    case 42: body1;
    case (let x, let y): body2;
    case Point { x, y }: body3;
    default: body4;
}
```

Semantics: evaluate `expr` once, test patterns in source order, bind
destructured names only for the matching branch.

### Expression form

```c
auto s = match (shape) {
    case Circle c: c.r;
    case Rect { w, h }: w * h;
    default: 0;
};
```

Requires a common result type; use explicit casts or `auto` inference
rules from tuples.

## Pattern kinds

1. **Literal** (`42`, `3.14`, `"hi"`, `'c'`, enum constants).
2. **Wildcard** (`_`): matches anything, binds nothing. Allowed only
   when `_` is not an existing identifier in scope.
3. **Binding** (`let x` or bare `x` in D mode): binds the matched value.
4. **Ignore operator** (`@_`): ignores a tuple element or struct field
   in destructuring, e.g. `case (x, @_, y):`.
5. **Tuple destructuring** (`(a, b, c)`).
6. **Struct destructuring** (`Point { x, y }`).
7. **Enum/tagged-union** patterns (`Tag.Circle c`, `Option.Some v`).
8. **Guards** (optional, M2): `case x if x > 0:`.

## Implementation steps

### 1. Parsing

- Add `match` as a contextual keyword in `-lang=d` (and optionally
  `-lang=c` behind a flag).
- Parse `match (expr) { case ... }` in statement position and in
  expression position after `=`.
- Parse each pattern into a small descriptor (tag, payload), not an
  AST.

### 2. Lowering

- Evaluate `expr` into a hidden temporary once.
- Lower each `case` to:
  - a tag/literal comparison for enums and integers,
  - a field offset comparison for structs,
  - tuple element comparisons for tuples,
  - a body block with bindings mapped to temporaries or direct field
    accesses.
- Lower `default` last; omit it when exhaustiveness is proven.

### 3. Bindings and scope

- Bindings live in a new scope covering only the case body.
- A binding name shadows outer names in that body.
- `let` is required in C mode; optional in D mode.
- `@_` binds nothing and suppresses unused warnings.

### 4. Exhaustiveness

- Integers/enums: collect covered constants; require a `default` unless
  every enumerator is covered.
- `_Bool`: `true`/`false` covers it.
- Tuples/structs: exhaustiveness only with a wildcard or all fields
  wildcarded.
- Report missing cases with a list.

### 5. Diagnostics

- `"non-exhaustive match; missing case 'X'"`.
- `"pattern binds 'x' twice"`.
- `"match expression has incompatible result types"`.
- `"@_ is not allowed outside destructuring patterns"`.

## Edge cases

- `match` on a function call with side effects: evaluate once.
- Nested patterns and nested tuples.
- Pointers and slices: match by value/identity, not contents, unless a
  slice-content pattern is added later.
- Floating-point patterns: exact comparison only; document.
- `break`/`continue`/`return` inside a case body.
- Fall-through: not allowed; every case is independent.
- Empty `match` and only-`default`.
- Duplicate literal cases: reject.
- Pattern ordering and unreachable patterns: warn.

## Test plan

| file | asserts |
| --- | --- |
| `NN_match_literal.c` | integer/char/string/enum literal cases |
| `NN_match_binding.c` | `let x` binds and scopes correctly |
| `NN_match_tuple.c` | tuple destructuring, including `@_` |
| `NN_match_struct.c` | struct field patterns |
| `NN_match_exhaustive.c` | missing-case diagnostic |
| `NN_match_expr.c` | expression form result type |
| `NN_match_once.c` | scrutinee evaluated exactly once |
| `NN_match_nested.c` | nested tuple/struct patterns |

Use `-dt` for expected-error tests.

## Acceptance criteria

- [ ] `match` parses in statement and expression form.
- [ ] Literal, wildcard, binding, tuple, and struct patterns work.
- [ ] `@_` ignores elements without unused warnings.
- [ ] Exhaustiveness diagnostics are actionable.
- [ ] Scrutinee is evaluated once.
- [ ] Regression tests under `tests/tests2/`.
