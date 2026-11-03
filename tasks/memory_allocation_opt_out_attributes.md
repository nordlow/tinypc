# Memory Allocation Opt-Out Attributes

Introduce fine-grained attributes that let a function or scope opt out
of specific categories of memory allocation. Each attribute forbids
exactly one category, so they can be combined to express stricter
guarantees.

## 1. Stack Memory Opt-Out
-  **Forbids:** Stack allocation beyond fixed-size frames, such as
  variable-length arrays, `alloca`, and large automatic arrays or
  locals. Whether unbounded recursion is also covered is an open
  question (see below).
-  **Use cases:** Embedded systems with strict stack limits, kernel
  programming, and recursion-heavy code that needs predictable stack
  depth.

## 2. Scoped / Bounded Heap Memory Opt-Out
-  **Forbids:** Heap allocations with scoped or bounded lifetimes, such
  as explicit region-based or arena allocations that are freed
  deterministically at scope exit.
-  **Use cases:** Real-time loops and high-performance systems code
  that must avoid even structured dynamic allocation.

## 3. Garbage-Collected Memory Opt-Out (`@nogc`)
-  **Forbids:** Allocation of managed memory whose reclamation is
  handled by a runtime garbage collector with unbounded or
  non-deterministic lifetimes (similar to D's `@nogc`).
-  **Use cases:** Low-latency systems, deterministic execution time,
  and environments with no runtime GC.

## Rationale

Together, these attributes enable strict memory-usage guarantees for
real-time systems, embedded targets, and determinism-critical,
high-performance codebases.
