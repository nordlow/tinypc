# Purity and determinism attributes: `@pure`, `@det`, `@nodet`

Define the semantics of the already-parsed `@pure` attribute together
with a companion `@deterministic` (alias `@det`) attribute and its
opt-out `@nodet`.  `@pure` is recorded on the function type today but
neither constrains code nor enables optimisations yet; the purity half
of the rule set is discussed in
[pure_attribute_semantics.md](pure_attribute_semantics.md).

Two related but distinct guarantees are involved:

- [ ] **Purity** — the function is a mathematical function of its
  arguments.  It writes no global mutable state, performs no I/O, and
  calls no function that does, so the only thing an observer can see
  is the returned value and the objects handed to it.  Everything it
  reads or writes is reachable through its own parameters and locals,
  which means it may still mutate that memory in place (see below).
- [ ] **Determinism** — the values the function computes depend only on
  its arguments and immutable state.  It does not let raw memory
  addresses, the clock, the environment, or thread interleaving
  influence a result, so code in its scope cannot vary with allocator
  behaviour, ASLR, or stack layout.

They are orthogonal in principle: determinism constrains the values a
function computes, purity constrains the effects it has.  A
`@deterministic` function may still increment a counter passed by
pointer, and in principle a pure function could return a value derived
from an address.  The language deliberately does not allow that
combination by default, which is what `@nodet` exists for.

## Purity is the mathematical definition

`@pure` is the mathematical notion of a pure function: the result is a
function of the inputs and nothing else, and there are no observable
effects beyond those inputs.  Crucially, "the inputs" include the
memory the caller hands over, so mutating that memory in place is part
of the input-to-output mapping rather than a side effect.

Sorting by reference is the canonical example:

```c
/* Pure: a[0..n) is both input and output, and nothing else is
   touched. */
void sort(int *a, int n) @pure
{
    /* quicksort, in place */
}
```

Viewed mathematically, `sort` maps the array contents before the call
to the array contents after it.  It is referentially transparent as
long as callers treat the argument as a value: the same contents in
give the same contents out, on every run, and no other state changes.
The same reasoning covers `reverse`, a hash-table insert into a
caller-owned table, `memcpy`-style fills, and any other algorithm
whose output parameters are pointers or slices.

The alternative — requiring purity to mean "returns a new object and
mutates nothing" — would forbid most in-place algorithms and force
allocation, which is the opposite of what a small C compiler should
require.  It is also weaker in practice: copying a struct only to
avoid writing through a pointer is not more pure, just more expensive.
This definition of functional purity is inspired by D's: a `pure`
function may modify its arguments (output parameters by reference,
such as for algorithms like quick sort) as long as it does not touch
mutable global state.

This model distinguishes between:
- [ ] **Weak purity**: a function qualified as `@pure` (may modify its
  arguments/output parameters by reference).
- [ ] **Strong purity**: a weakly pure function where all its
  (`immutable`) parameters cannot change during the lifetime of the
  function's execution.

Two consequences:

- [ ] Mutating a parameter is allowed even though the caller can observe
  it; the guarantee is that nothing *outside* the arguments is
  observable.
- [ ] Purity does not imply `const`: a `@pure` in-place algorithm takes
  `int *`, not `const int *`.
## Proposal

- [ ] `@deterministic` (alias `@det`) demands determinism at function or
  block scope.  Address-dependent results (ordering by address,
  hashing addresses, printing addresses) silently break
  reproducibility, so they are rejected rather than merely documented.
- [ ] `@pure` implies `@deterministic`.  A pure function that reports
  different results across runs for the same arguments is not a
  useful building block for caching, memoisation, or reproducible
  builds, and purity-based optimisation decisions become hard to
  reason about.  Non-determinism therefore has to be requested
  explicitly.
- [ ] Non-determinism is opt-in via `@nodet`: a `@nodet` block, statement,
  or function declaration is the only place where non-deterministic
  operations are permitted inside a `@pure` or `@det` scope.

| attribute      | side effects | address / ambient dependence |
| -------------- | ------------ | ---------------------------- |
| none           | allowed      | allowed                      |
| `@det`         | allowed      | forbidden                    |
| `@pure`        | forbidden    | forbidden                    |
| `@pure @nodet` | forbidden    | allowed (programmer asserts) |
| `@det @nodet`  | allowed      | allowed (programmer asserts) |

`@nodet` relaxes only the determinism requirement.  It does not allow
writes or impure calls; dropping those requires giving up `@pure`.
This keeps the two opt-outs separable: `@trusted` turns off safety
checks, `@nodet` turns off determinism checks.

## Rationale

`@pure` code, and deterministic code generally, must not inadvertently
depend on runtime pointer addresses (which differ across runs and
platforms due to ASLR and stack layout).  Comparing or ordering non-null
runtime addresses, hashing addresses, or printing addresses silently
breaks reproducibility.  However, comparing pointer values against zero
(the null pointer constant, `NULL`, or `0`) is fully allowed and
deterministic since the null address is invariant across runs and platforms.

Determinism is also what makes a result cacheable, comparable across
runs, and stably testable.  Making it part of `@pure` avoids the
inconsistent state where a function is declared pure and therefore
reorderable and CSE-able, yet returns results that differ between two
runs of the same binary.

## Allowed

- [ ] Dereference (`*p`, `p->f`), assignment, and passing pointers.
- [ ] Array/slice indexing and slicing (`p[i]`, `&p[i]`, `p[a..b]`),
  despite their desugaring to offset arithmetic.
- [ ] Writing through a pointer, slice, or other output parameter,
  including in-place algorithms such as the `sort` above.
- [ ] Pointer equality and relational comparison against zero / null (`p == NULL`, `p != NULL`, `!p`), as the null address is invariant across runs and platforms.
- [ ] Allocation, in `@det` code: the address itself is a handle that may
  be stored, dereferenced, and passed on.  Only letting that address
  leak into a computed value is forbidden.

## Forbidden (compile-time error)

- [ ] Raw pointer arithmetic: `p + n`, `p - n`, `p1 - p2`, `++p`, `--p`,
  `p += n`.
- [ ] Relational comparison: `p1 < p2`, `<=`, `>`, `>=`.
- [ ] Equality between two non-null pointers (`p1 == p2`). [DECIDE]
- [ ] Pointer <-> integer casts in either direction (`size_t`,
  `uintptr_t`, `intptr_t`, any integer type).
- [ ] Formatting or hashing a pointer value (`%p`, pointer used as hash
  input or ordered-map key).
- [ ] Reinterpreting pointer bits via `union` punning or `memcpy`/`memcmp`
  of pointer objects.
- [ ] Calling non-`@det` functions (including extern/libc) unless declared
  `@det` or wrapped in `@nodet`.  Extern declarations are presumed
  non-deterministic, which covers `time()`, `rand()`, `getenv()`, and
  friends without special-casing them.
- [ ] Inside `@pure`, additionally: writes to global mutable state, I/O,
  volatile/atomic accesses, and calls to functions that are not
  `@pure` (see [purity semantics](pure_attribute_semantics.md)).
  This applies to writes outside the memory reachable from the
  function's parameters; writing through an output parameter is
  allowed, as described above.

## `@nodet`

`@nodet` is an unchecked assertion by the programmer, in the spirit of
`@trusted`, and should be scoped as narrowly as possible:

```c
int bucket(void *p) @pure
{
    /* Hash the address to pick a bucket, but return an
       order-independent result. */
    @nodet { return (int)(uintptr_t)p & 0x3f; }
}
```

- [ ] On a function, `@nodet` means the function is not `@det`, so `@det`
  code and other `@pure` functions can only call it from a `@nodet`
  region.
- [ ] On a block or statement, it suppresses determinism diagnostics for
  that region only.
- [ ] It is inherited into nested blocks and lambdas, but not into a
  non-`@nodet` function called from inside the region.
- [ ] Repetition should be flagged: a warning for `@nodet` in code that is
  not itself `@pure`/`@det`, where it has no effect.

## Open questions

- [ ] What exactly `@pure` implies beyond no side effects: D-style weak
  purity (immutable globals readable) or strong purity.  See
  [pure_attribute_semantics.md](pure_attribute_semantics.md).
- [ ] Should `@nodet` be usable on a single expression
  (`x = @nodet p ? 1 : 0`), or only on statements and blocks?
- [ ] Block syntax (`@det { ... }`) and inheritance into lambdas and macro
  expansions.
- [ ] Escape hatch for runtime/allocator code that is deterministic but
  cannot be annotated (libc wrappers): `@trusted`, `@nodet`, or both?
- [ ] Function pointers: forbid comparison and casting, and require the
  pointee type to be `@det` before it may be called from `@det` code?
- [ ] Should `malloc` be callable from `@pure` (D allows allocation in
  pure code), or only from `@det`?
- [ ] Aliasing: may a `@pure` function write through a parameter that is
  also reachable from immutable global state (D forbids this), and
  does `restrict` change the answer?
- [ ] Diagnostics: one error per forbidden category with a fix-it hint
  ("use slice indexing instead of pointer arithmetic"), and a note
  pointing at the `@pure`/`@det` declaration that introduced the
  requirement.
- [ ] Does the `@pure` -> `@det` inference apply to declarations without a
  body, forwarding the requirement to the caller's link-time view?

## Tests

- [ ] One negative test per forbidden operation; positive tests for
  indexing, slicing, null checks, and dereference.
- [ ] Verify `p[i]` and `&p[i]` are accepted while `*(p + i)` is rejected.
- [ ] `@pure` function that computes an address-dependent value is
  rejected; the same function with `@nodet` compiles.
- [ ] `@pure` in-place sort writes through its `int *` parameter and
  compiles; writing to a file-scope array instead is rejected.
- [ ] `@det` function may write through an output parameter, and `@pure`
  may too; neither may write to global mutable state.
- [ ] Calling a non-`@det` extern from `@det` code is rejected; the call
  inside `@nodet` is accepted.
- [ ] `@nodet` on one statement does not silence diagnostics in the
  surrounding function.
- [ ] Forward declarations (`int f(int x) @pure;`) carry the implied
  `@det`, so a non-deterministic body is rejected at the definition.
