# Task: D-Style Expression Functions (`=>` Syntax) and Representation Analysis

This task defines the syntax, parsing rules, lowering strategy, and
representation analysis for D-style expression functions (single-expression
functions using `=>`) in TinyPC (`-lang=d` mode).

## Motivation

In D and modern systems languages, expression functions allow concise
definition of simple functions without requiring braces and explicit
`return` statements:

```d
auto f(int x) => x + 1;
int add(int a, int b) => a + b;
```

Adding this syntax to TinyPC improves ergonomics for short utility
functions, math helpers, and template/inlined wrappers.

---

## Grammar & Syntax

1. **Function Definition with Fat Arrow (`=>`)**:
   Instead of a compound statement `{ ... }` as the function body, a
   function definition may use the `=>` operator followed by an expression
   and a semicolon:
   ```c
   [storage] return_type function_name(parameters) => expression;
   ```

2. **Inferred Return Type (`auto`)**: When combined with `auto`, the
   return type is deduced from the type of the expression on the
   right-hand side of `=>`.

---

## Representation Analysis: Does it Deserve a Special Representation?

**Decision: No special representation is needed.**

### Rationale:
1. **Syntactic Sugar (Desugaring):**
   An expression function `auto f(int x) => expr;` is semantically identical
   to a standard function with a compound body containing a single return
   statement:
   ```c
   auto f(int x) { return expr; }
   ```
2. **Implementation Strategy:**
   During function definition parsing in `tccgen.c`, when the parser encounters
   `=>` where a function body (`{`) is expected:
   - [ ] Parse the return expression `expr`.
   - [ ] Desugar it internally into an implicit `return expr;`
         statement.
   - [ ] **No Scope Overhead:** The function does not introduce a new
         lexical block scope; parameters reside directly in the
         function scope and the expression evaluates within it.
   - [ ] Hand off the resulting AST node stream to the existing
         function compiler, type checker, return type deduction
         engine, and code generator.
3. **Benefits of Zero Special Representation:**
   - [ ] **Zero Backend Impact:** Code generators (`x86_64-gen.c`,
         etc.), ABIs, stack frames, and debugging stabs require zero
         modifications.
   - [ ] **Zero Symbol Table Bloat:** `Sym` and `FuncAttr` do not need
         extra flags or AST node variants.
   - [ ] **Full Feature Reuse:** Automatically inherits support for
         local variables, temporaries, return type inference (`auto`),
         cleanup guards, and debug info from normal functions.


## Compiler Optimization Opportunities

While expression functions desugar into standard functions without
requiring special runtime representation or scope overhead, their
restricted single-expression form unlocks several compiler
optimizations:

1. **Trivial Inlining:** Because the body is guaranteed to be a single
   expression without complex control flow or local variable blocks,
   small expression functions are ideal candidates for aggressive
   automatic inlining.
2. **Automatic Purity & CTFE Inference:** Analyzing whether a function
   is `pure` or eligible for Compile-Time Function Execution (CTFE) is
   simplified, as side-effect analysis reduces to inspecting a single
   expression.
3. **Trivial Tail-Call Optimization (TCO):** When the expression is a
   function call (e.g., `int f(int x) => g(x);`), it is structurally
   in terminal tail position, allowing trivial conversion to a tail
   jump (`jmp`).
4. **Zero-Scope Overhead:** Avoiding compound block allocation (`{
   ... }`) and symbol scope push/pop operations saves memory and CPU
   cycles during parsing and semantic analysis.

## Implementation Plan

1. **Parser Extension (`tccgen.c`):**
   - [ ] In function definition parsing, check for `TOK_DARROW` (`=>`)
         in place of `{`.
   - [ ] Parse the single expression, build an implicit `return`
         statement node, and compile it as a standard function body.
2. **Return Type Inference (`auto`):**
   - [ ] Leverage existing type deduction rules for `auto` return
         types by evaluating the expression's type during
         parsing/analysis.
3. **Regression Tests:**
   - [ ] Add test cases under `tests/tests2/` covering basic
         expression functions, `auto` return type deduction, and
         multi-parameter expressions.
