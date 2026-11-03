# Task: Memory Allocation Opt-Out Attributes

> **Status:** Proposed.

Introduce fine-grained attributes that let a function or scope opt out
of specific categories of memory allocation. Each attribute forbids
exactly one category, so they can be combined to express stricter
guarantees.

## Attribute names and syntax

Use the existing `@`-attribute machinery (`parse_d_attributes()` and
`AttrDef`):

| attribute | forbids |
| --- | --- |
| `@nostack` | dynamic/stack allocations beyond a fixed frame |
| `@noscoped` | scoped/bounded/arena heap allocations |
| `@nogc` | garbage-collected (managed) allocations |

Spellings: `@nostack`, `@noscoped`, `@nogc`. Allow a negation form
(`@nogc(false)`) only if the existing attribute parser already supports
boolean arguments; otherwise keep them boolean markers.

Scopes: function attributes, block attributes (`@nogc { ... }`), and
file-scope default attributes if the attribute system supports them.

## 1. Stack memory opt-out (`@nostack`)

**Forbids:** stack allocation beyond fixed-size frames:

- variable-length arrays (`int a[n]`),
- `alloca()`,
- large automatic arrays or locals whose size is not a compile-time
  constant,
- optional (open question): recursion depth beyond a configured limit.

**Use cases:** embedded systems with strict stack limits, kernel
programming, and recursion-heavy code that needs predictable stack
depth.

**Implementation notes:**

- Detect VLAs in `decl()` / type parsing where `VT_VLA` is set.
- Detect `alloca` by symbol identity after header resolution; do not
  match by name alone.
- Emit `"@nostack: variable-length array is not allowed"` with the
  declaration location.
- Do not reject ordinary fixed-size locals; `@nostack` is about
  unbounded/dynamic stack use, not about frame size.

## 2. Scoped / bounded heap memory opt-out (`@noscoped`)

**Forbids:** heap allocations with scoped or bounded lifetimes, such as
explicit region-based or arena allocations freed deterministically at
scope exit.

**Use cases:** real-time loops and high-performance systems code that
must avoid even structured dynamic allocation.

**Implementation notes:**

- There is no arena/region API in the compiler yet. Define the
  recognised allocation functions/attributes first (for example
  `@scoped_alloc` functions or a known `arena_alloc` symbol type).
- Until that API exists, `@noscoped` rejects calls to functions marked
  `@allocates(scoped)` and documents the limitation.
- Keep the check lexical: a call inside an `@noscoped` function/block
  that targets an unknown function is allowed, because the compiler
  cannot prove allocation.

## 3. GC memory opt-out (`@nogc`)

**Forbids:** allocation of managed memory whose reclamation is handled
by a runtime garbage collector.

**Use cases:** low-latency systems, deterministic execution time, and
environments with no runtime GC.

**Implementation notes:**

- TinyPC has no GC. Define the contract against a future runtime or a
  user-provided attribute on allocation functions:
  `@gcalloc` / `@gc`.
- `@nogc` rejects calls to `@gcalloc` functions and `new`-style
  allocation builtins once those exist.
- If no GC exists, the attribute is accepted and currently has no
  runtime effect; document that it is forward-looking.

## Cross-cutting implementation steps

1. Extend `AttrDef` with `nostack`, `noscoped`, `nogc` bits.
2. Parse the three spellings in `parse_d_attributes()` and in the
   C `__attribute__((...))` path where applicable.
3. Propagate the bits to `Sym`/`CType` so nested scopes inherit them.
4. Add check helpers invoked at:
   - VLA/`alloca` creation (`@nostack`),
   - call emission (`gfunc_call()` for `@noscoped`/`@nogc`),
   - block entry/exit for block-scoped attributes.
5. Report errors with `tcc_error()` and the offending location; keep
   warnings available behind `-Walloc`.
6. Document each attribute and its current limitations in `README.md`.

## Edge cases

- Nested scopes: inner `@nogc` inside outer `@nogc` is redundant, not
  an error.
- Function pointers: the compiler cannot see the target's attribute;
  document this hole or require a typed function pointer attribute.
- `@system`/`@trusted` interaction: safety escape hatches do not lift
  allocation restrictions unless explicitly specified.
- Recursion (`@nostack` open question): if implemented, count
  self-recursive calls and reject only when `-fmax-stack-recursion`
  is exceeded.
- Inline functions: check the caller's attributes after inlining, not
  only the callee's.

## Test plan

| file | asserts |
| --- | --- |
| `NN_nostack_vla.c` | VLA inside `@nostack` is rejected |
| `NN_nostack_alloca.c` | `alloca` inside `@nostack` is rejected |
| `NN_nostack_ok.c` | fixed-size locals are accepted |
| `NN_nogc_call.c` | call to a `@gcalloc` function is rejected |
| `NN_noscoped_call.c` | call to a scoped allocator is rejected |
| `NN_attr_nested.c` | block-scope propagation works |

## Rationale

Together, these attributes enable strict memory-usage guarantees for
real-time systems, embedded targets, and determinism-critical,
high-performance codebases.

## Acceptance criteria

- [ ] Attributes parse and propagate through nested scopes.
- [ ] Each attribute rejects its category with an actionable error.
- [ ] No false positives on ordinary fixed-size locals.
- [ ] Documented limitations for APIs that do not exist yet.
- [ ] Regression tests under `tests/tests2/`.
