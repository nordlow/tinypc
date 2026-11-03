# Namespaces and Scoping Syntax in C and D (`::`)

> **Status:** Proposed. Design investigation first.

Investigate adding syntax for namespaces in both C and D (proposal is
`::`) to prevent identifier collisions between existing keywords and
user-defined identifiers in C, as well as keywords of new constructs
(currently for D) that have been added such as `debug` and `version`.

## Motivation

As TPC extends support for features from both C and D, identifier
collisions frequently arise:

- Existing C keywords vs user-defined identifiers.
- New construct keywords added for D (such as `debug`, `version`,
  `immutable`, `scope`) conflicting with existing user code
  identifiers.

## Design questions

1. Is `C::name` / `D::name` a *keyword escape* (resolve `name` as the
   language keyword) or a *module namespace* (a separate symbol
   table)?
2. Must the prefix be reserved? `::` is not valid C today, so the
   lexer can claim it without breaking existing code.
3. How do C and D sources share one TU? Is there one symbol table with
   language-tagged entries, or two?
4. Do user-defined namespaces stack (`a::b::name`)?
5. What is the fallback when a name exists in both namespaces?

## Proposed syntax

- C keyword escape: `C::_Complex`, `C::static`, `C::debug`.
- D keyword escape: `D::debug`, `D::version`, `D::immutable`,
  `D::scope`, `D::__traits`.
- Optional user modules: `mymod::name`, resolved in the current
  language's namespace.

## Implementation steps

### 1. Lexer

- Add `TOK_COLONCOLON` for `::` in `tcctok.h`.
- Lex `::` as one token; keep `:` unchanged.
- Update the token-string table and any token pretty-printer.

### 2. Parser

- In `primary()` / `unary()` handling of identifiers, after an
  identifier (or keyword) check for `TOK_COLONCOLON`.
- Recognise the reserved prefixes `C`/`c`/`_c`/`__c` and
  `D`/`d`/`_d`/`__d`.
- Parse `prefix :: name` into a "qualified identifier" node without
  building an AST: resolve immediately to the symbol or keyword.
- Reject `::` with no left or right operand with a clear error.

### 3. Keyword lookup

- Add a helper `int keyword_in_lang(int lang, const char *name)` that
  maps a name to its token only for that language.
- When a qualified name resolves to a keyword, return the token as if
  it had been lexed directly.
- When it resolves to a symbol, perform the normal `sym_find()` lookup.

### 4. Symbol resolution

- M1: no user modules; only language escapes.
- M2: add a namespace prefix to `Sym` or a per-namespace hash table.
- Resolution order: explicit namespace, then current namespace, then
  global. Report ambiguity explicitly.

### 5. Diagnostics

- `"unknown namespace 'foo'"`.
- `"'name' is not a D keyword"`.
- `"ambiguous 'name' in namespaces 'a' and 'b'"`.

### 6. Documentation

- `README.md`: a table of available escapes and examples.
- Note that `::` is a TPC extension and is not valid ISO C.

## Edge cases

- `a ? b : :c` tokenisation: `?:` and `::` must not merge.
- Digraphs/trigraphs: `%:%:` is not `::`.
- Labels and `goto`: namespaces do not apply.
- Macro expansion producing `::`.
- `C::` in `-lang=d` and `D::` in `-lang=c`: allow or reject? Propose
  allow, because the whole point is cross-language access.
- `sizeof(C::int)` and other type contexts.

## Test plan

| file | asserts |
| --- | --- |
| `NN_ns_c_keyword.c` | `C::debug` resolves as a C identifier/keyword, not D |
| `NN_ns_d_keyword.c` | `D::version` resolves in C mode |
| `NN_ns_undefined.c` | unknown namespace and unknown member errors |
| `NN_ns_token_split.c` | `?:` still lexes correctly next to `::` |
| `NN_ns_macro.c` | macro-generated `::` resolves |
| `NN_ns_user_module.c` | M2 user namespace lookup |

## Acceptance criteria

- [ ] `::` lexes as a single token.
- [ ] `C::` and `D::` resolve keywords in both language modes.
- [ ] Unknown names/namespaces produce actionable errors.
- [ ] No change to existing C/D programs.
- [ ] Regression tests under `tests/tests2/`.
