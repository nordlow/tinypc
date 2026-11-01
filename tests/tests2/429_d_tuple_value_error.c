/* Compile-time value tuples (P2): every misuse must be diagnosed.  */

#if defined test_non_constant_element
int f(int x) { return $(x); }

#elif defined test_non_constant_tuple_element
int f(int x) { return $(1, x)[1]; }

#elif defined test_void_element
int f(void) { return $((void)0, 1)[0]; }

#elif defined test_struct_element
int f(void)
{
    static struct { int a; } s;
    return $(s)[0];
}

#elif defined test_string_element
int f(void) { return $("ab")[0]; }

#elif defined test_string_tuple_element
int f(void) { return $(1, "ab")[1]; }

#elif defined test_function_element
int f(void);
int g(void) { return $(f, 1)[0]; }

#elif defined test_nested_non_constant_element
int f(int x) { return $($(1, 2)[0], x)[1]; }

#elif defined test_empty_tuple
int f(void) { return $(); }

#elif defined test_leading_comma
int f(void) { return $(, 1)[0]; }

#elif defined test_index_out_of_range
int f(void) { return $(1, 2)[2]; }

#elif defined test_negative_index
int f(void) { return $(1, 2)[-1]; }

#elif defined test_index_not_constant
int f(int i) { return $(1, 2)[i]; }

#elif defined test_scalar_subscript
int f(void) { return $(1)[0]; }

#elif defined test_non_constant_if
int f(int x) { $if (x) return 1; else return 2; }

#elif defined test_if_without_parens
int f(void) { $if 1 return 1; }

#elif defined test_if_empty_condition
int f(void) { $if () return 1; }

#elif defined test_if_colon_declaration
int f(void) { $if (1) : int x; }

#elif defined test_if_braced_scope
int f(void) { $if (1) { int x = 1; } return x; }

#elif defined test_if_redeclaration
int f(void) { $if (1) int x = 1; int x = 2; return x; }
#endif
