# Specification: Explicit Memory Allocation (`allocator.new`) and Construction (`@init`)

> **Status:** Proposed. Depends on the `@init` method and attribute
> support; see `tasks/deterministic_attribute.md`.

## Goal

Define a clear, memory-safe syntax separating raw memory allocation from
object initialization in TinyPC (TPC), using explicit allocator
instances and `@init` construction methods.

## Definitions & distinction

- **Allocator:** an abstract interface or strategy that provides raw
  memory blocks (heap, free-list, pool).
- **Arena:** a concrete allocator that allocates monotonically via a
  bump pointer from a contiguous region and frees all objects at once.
- **Initialization:** writing a valid value into already-allocated
  storage, performed by `@init`.

## Syntax specification

1. **Memory allocation**
   - Syntax: `allocator.new(&x, CTOR_ARGS)`
   - `allocator` is an lvalue of an allocator type (struct or pointer).
   - `&x` is an lvalue of pointer type `T*`; storage for `T` is
     allocated and the address stored in `x`.
   - The allocated block is uninitialized; reading it before `@init` is
     a compile-time error where the compiler can prove it.
2. **Object initialization**
   - Syntax: `x.@init(CTOR_ARGS)`
   - Calls the initializer method declared on `T`.
   - `x` must be a pointer to `T` whose storage came from a `.new`
     call, or a local/aggregate that is being constructed.
3. **Destruction (optional, M2)**
   - `x.@deinit()` or `destroy(x)` for types that need it.

## Example usage

```c
Arena arena = make_arena(buffer, sizeof(buffer));
MyStruct* obj;

// 1. Allocate uninitialized memory from the custom allocator.
arena.new(&obj);

// 2. Initialize state safely.
obj.@init(42);

// 3. Optional explicit teardown.
obj.@deinit();
```

## Implementation steps

### 1. Type and attribute plumbing

- Add `@init` and `@deinit` to the attribute/method lookup tables.
- Attach an `has_init`/`needs_init` bit to `CType` or the struct
  definition, set when a type declares `@init`.
- Reject `@init` on non-struct/union types unless a future scalar
  constructor design is added.

### 2. Parse `allocator.new`

- In postfix/member parsing, recognise `.new` after an allocator
  expression.
- Parse `(&x, ARGS)`:
  - the first argument must be an address-of expression,
  - infer `T` from `x`'s pointer type,
  - record `ARGS` for the call.
- Desugar to a call:
  `x = (T*)allocator_alloc(&allocator, sizeof(T), alignof(T));`
  followed by optional `x.@init(ARGS)` if the type needs it.

### 3. Parse `x.@init(ARGS)`

- Recognise `@init` after `.` in postfix parsing.
- Resolve the method on `T`; error if missing.
- Type-check `ARGS` against the `@init` signature.
- Reject a second `@init` on the same object in the same scope.

### 4. Diagnostics

- `"allocation of type 'T' requires @init"`.
- `"@init called twice on 'x'"`.
- `"x.new expects an address-of lvalue"`.
- `"allocator type has no .new method"`.
- `"cannot allocate type 'T' with @scoped/@nocopy constraints"`.

### 5. Documentation

- Add a `README.md` section with the arena example and lifetime rules.
- Document which allocator types are recognised and how users declare
  custom ones.

## Edge cases

- Allocation failure (`.new` returns null/does not store): define
  whether `x` is set to null and how `@init` behaves.
- Zero-sized types and flexible arrays.
- Arrays: `allocator.new(&arr_ptr, N)` for `T[N]`.
- Nested allocations: `arena.new(&child)` inside a parent `@init`.
- Exceptions/longjmp: leaked arena memory on early exit.
- `@nocopy`/`@scoped` types: reject `.new` unless explicitly allowed.
- Generic/allocator-interface types (if later added).
- Reusing `x` that already points at live storage: require an explicit
  `@deinit`/`free` first.

## Test plan

| file | asserts |
| --- | --- |
| `NN_alloc_new_basic.c` | `arena.new(&obj)` allocates and stores a valid pointer |
| `NN_alloc_init_basic.c` | `obj.@init(42)` initializes the object |
| `NN_alloc_init_missing.c` | using an object before `@init` is diagnosed |
| `NN_alloc_init_twice.c` | a second `@init` is rejected |
| `NN_alloc_new_bad_arg.c` | non-lvalue/non-pointer first argument is rejected |
| `NN_alloc_custom.c` | a user-defined allocator type works |
| `NN_alloc_scoped_error.c` | `@scoped`/`@nocopy` interaction is enforced |

Use `-dt` for expected-error tests.

## Acceptance criteria

- [ ] `allocator.new(&x, ARGS)` works for custom allocators and arenas.
- [ ] `x.@init(ARGS)` performs checked construction.
- [ ] Missing/duplicate `@init` produces actionable diagnostics.
- [ ] `@nocopy`/`@scoped` interactions are enforced.
- [ ] Regression tests under `tests/tests2/`.
