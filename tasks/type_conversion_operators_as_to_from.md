# Type Conversion and Lazy Serialization Operators (`@as`, `@to`, `@from`)

> **Status:** Preliminary draft. Needs further consideration and design refinement
> before becoming a ready specification.

Investigate and design operators for explicit type conversion and lazy
serialization/deserialization across formats (such as JSON and YAML),
incorporating D-style ranges / Rust-style iterators and language precedents
for lazy constructor/from-conversions.

## Proposed Operators & Semantics

-  **`T @as (FORMAT) U` (Lazy View / Iteration):**
  -  Lazy conversion or streaming representation of `T` as `U` in a given
    `FORMAT` (e.g., JSON, YAML, binary).
  -  Yields data in the form of a D-style range or Rust-style iterator of
    `byte`s (or typed chunks/tokens).
  -  Avoids full materialization in memory; parsing and serialization occur
    lazily on demand during iteration.

-  **`T @to (FORMAT) U` (Eager Conversion):**
  -  Eagerly converts `T` to `U` using `FORMAT`.
  -  Materializes the full target representation in memory immediately.

-  **`U @from T` (Construction / Inverse Mapping):**
  -  Constructs an instance of `U` from `T` (or parses `T` into `T`).
  - [ ] Equivalent to a constructor or `TryFrom`/`From` trait implementation.

## Lazy Version of `@from` in Existing Languages

Are there languages modeling a lazy version of `@from`?
-  **Rust:** `FromIterator` allows collecting lazily from an iterator into a
  collection (`U::from_iter(iter)`). Serde also supports streaming
  deserialization from an IO reader or iterator (`Deserializer::into_iter`
  or `StreamDeserializer`), producing lazy items.
-  **C++:** `std::ranges` adaptors and generators (`std::generator` in
  C++23) allow lazy construction/transformation views from input streams or
  ranges (e.g., parsing a stream lazily into objects).
-  **Haskell:** Lazy `ByteString` and lazy parsing libraries (like
  `attoparsec` or `megaparsec` streaming) construct structures or parse
  lazily from input sources on demand.
-  **D:** `std.range` and range-based decoders (e.g., parsing JSON/XML via
  range primitives `empty`, `front`, `popFront`), where a range acts as a
  lazy producer of structured elements from raw bytes.

## Operator Design Considerations

-  **Syntax & Precedence:**
  -  Using `@as`, `@to`, and `@from` as infix operators or keyword-prefixed
    operators.
  - [ ] Interaction with existing operator precedence rules in C and D.

- [ ] **Open Questions:**
  1. **Memory Ownership & Lifetime:** In lazy ranges (`T @as (JSON) U`),
     who owns the underlying buffers during iteration? Do ranges borrow or
     own their state?
  2. **Error Handling:** When does parsing/conversion error occur? Eager
     (`@to`) fails upfront; lazy (`@as`) defers errors to iteration time,
     raising exceptions or returning `Result`/`Expected` wrappers on
     `front` or `popFront`.
  3. **Bidirectionality & Symmetry:** How do `@to` and `@from` interact?
     Is `u @from t` precisely inverse to `t @to u`, and how does lazy `@as`
     fit into the conversion matrix?
  4. **Format Specialization:** How are formats (`JSON`, `YAML`, `Binary`,
     etc.) specified as part of the operator or trait system?
