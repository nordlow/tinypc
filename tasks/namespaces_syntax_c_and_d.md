# Namespaces and Scoping Syntax in C and D (`::`)

Investigate adding syntax for namespaces in both C and D (proposal is
`::`) to prevent identifier collisions between existing keywords and
user-defined identifiers in C, as well as keywords of new constructs
(currently for D) that have been added such as `debug` and `version`.

## Motivation

As TPC extends support for features from both C and D, identifier
collisions frequently arise:
- [ ] Existing C keywords vs user-defined identifiers.
- [ ] New construct keywords added for D (such as `debug`, `version`,
  `immutable`, `scope`) conflicting with existing user code identifiers.

## Proposed Solutions

Introduce a namespace / prefix syntax such as:
- [ ] C: `C::`, `c::`, `_c::`, `__c::`
- [ ] D: `D::`, `d::`, `_d::`, `__d::`

Or general module/language namespace scoping via `::` to explicitly
qualify symbols (e.g. `C::printf`, `D::debug`).

## Impact on Symbol Resolution

- [ ] Parser and lexer updates to recognize `::` as a scope resolution
  operator.
- [ ] Symbol table (`Sym`) adjustments to scope identifiers within language
  or module namespaces.
- [ ] Resolution logic in `tccgen.c` for qualified lookups.
