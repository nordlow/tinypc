# Specification: Explicit Memory Allocation (`allocator.new`) and Construction (`@init`)

## Goal

Define a clear, memory-safe syntax separating raw memory allocation from
object initialization in TinyPC (TPC), utilizing explicit allocator
instances and `@init` construction methods.

## Definitions & Distinction

- **Allocator:** An abstract interface or strategy responsible for
  providing raw memory blocks (e.g., heap, heap-free-list, pool).
- **Arena:** A concrete allocator strategy that allocates memory
  monotonically via bump-pointer from a contiguous region and deallocates
  all objects simultaneously.

## Syntax Specification

1. **Memory Allocation:**
   - Syntax: `allocator.new(&x, CTOR_ARGS)`
   - Semantics: Allocates uninitialized storage for the type of `x` (a
   pointer `T*`) using the specified `allocator`, assigns the resulting
   address to `x`.

2. **Object Initialization:**
   - Syntax: `x.@init(CTOR_ARGS)`
   - Semantics: Invokes the constructor/initializer method on the newly
   allocated object instance `x`.

## Example Usage

```c
Arena arena = make_arena(buffer, sizeof(buffer));
MyStruct* obj;

// 1. Allocate uninitialized memory from custom allocator
arena.new(&obj, initial_val);

// 2. Initialize state safely
obj.@init(42);
```

## Acceptance Criteria

- **P1:** Support `allocator.new(&x, ARGS)` for safe typed allocation
  across custom allocators and arenas.
- **P2:** Support `x.@init(ARGS)` for explicit object construction.
- **P3:** Enforce compile-time diagnostics if `@init` is omitted on types
  requiring initialization or `@nocopy`/`@scoped` constraints.
- **Verification:** `./configure && make -j$(nproc) -B && make
  -j$(nproc) test` passes with regression tests in `tests/tests2/`.
