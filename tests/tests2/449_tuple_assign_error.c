/* Tuple destructuring assignment: every misuse must be diagnosed.  */

#if defined test_non_lvalue_literal
int f(void) { int a, b; (b, 1) = (1, 2); return a; }

#elif defined test_non_lvalue_expression
int f(void) { int a, b; (b, a + 1) = (1, 2); return a; }

#elif defined test_non_lvalue_void
int f(void) { int a, b; ((void)0, b) = (1, 2); return a; }

#elif defined test_too_many
int f(void) { int a, b; (a, b) = (1, 2, 3); return a; }

#elif defined test_too_few
int f(void) { int a, b; (a, b) = (1,); return a; }

#elif defined test_non_tuple_literal
int f(void) { int a, b; (a, b) = 1; return a; }

#elif defined test_non_tuple_object
int f(void) { int a, b, x = 1; (a, b) = x; return a; }

#elif defined test_function_arity
(int, int, int) three(void);
int f(void) { int a, b; (a, b) = three(); return a; }
#endif
