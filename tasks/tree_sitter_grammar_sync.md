# Task: Automatic Tree-Sitter `grammar.js` Synchronization with C/D Parser

> **Status:** Proposed tooling task. Prerequisite: tuple/slice syntax
> is stable enough to freeze a grammar.

## Goal

Automate the generation, updating, and validation of the Tree-sitter
grammar (`grammar.js`) in sync with extensions and additions to TinyPC's
C and D parser (tuples, slices, `foreach`, destructuring assignment,
scope guards, attributes).

## Scope & requirements

1. **Parser-grammar alignment**
   - Map TPC-specific extensions into Tree-sitter node rules:
     - tuples `(T1, T2)` and `$(...)`,
     - slices `T[]` and slice literals,
     - destructuring assignment `(a, b) = expr`,
     - `foreach`/`foreach_reverse`,
     - attributes (`@safe`, `@pure`, `@scoped`, ...),
     - `$run`, `$assert`, `unittest`, `version`.
   - Maintain a single source of truth for token spellings; do not
     duplicate keyword lists by hand.
2. **Automated sync/codegen**
   - Add a target `make treesitter-sync` or `tools/sync-grammar`.
   - Validate that every test file under `tests/tests2/` parses.
   - Fail CI when a construct parses in `tcc` but not in Tree-sitter.

## Implementation steps

1. **Inventory.** List every token/keyword added to `tcctok.h` and
   every parser construct that is not standard C.
2. **Keyword export.** Add a generator that extracts keywords from
   `tcctok.h` (or a shared keyword table) and emits the keyword list
   for `grammar.js`.
3. **Grammar rules.** Add/extend rules in `grammar.js`:
   - `declaration` for type-first arrays and slices,
   - `tuple_type`, `tuple_literal`, `destructuring_assignment`,
   - `foreach_statement`, `match_statement` (when landed),
   - `attribute` and `attribute_block`.
4. **Corpus test.** `tools/sync-grammar --check` parses a curated
   corpus:
   - all `tests/tests2/*.c`,
   - a set of negative cases that must produce grammar errors.
5. **Build integration.** Add a `ninja`/`make` target and document it;
   keep it opt-in until the grammar is complete.
6. **Drift detection.** Commit the generated grammar or a checksum;
   CI regenerates and compares.

## Edge cases

- Contextual keywords (`$run`, `debug`, `version`) and `-lang=c` vs
  `-lang=d` differences.
- Macros that expand to syntax: Tree-sitter sees the raw token stream,
  so preprocessor-heavy tests may not be meaningful.
- `#if` blocks and `#include` handling.
- Digraphs/trigraphs and line continuations.
- Comments and attributes between tokens.
- Empty parameter lists and K&R declarations.
- Multiple `tests2` files share a prefix (`425+_...`); use the
  canonical file.

## Test plan

| check | asserts |
| --- | --- |
| `tools/sync-grammar --check` | all corpus files parse |
| keyword drift test | `tcctok.h` keywords match generated list |
| negative corpus | malformed constructs are rejected |
| CI target | fails on newly added unparsed syntax |

## Acceptance criteria

- [ ] `grammar.js` covers tuples, slices, destructuring, `foreach`,
      attributes, and compile-time constructs.
- [ ] A build check fails on parser/grammar drift.
- [ ] `tree-sitter parse` succeeds on the comprehensive test corpus.
- [ ] Generated keyword list has a single source of truth.
