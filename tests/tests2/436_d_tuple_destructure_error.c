/* Tuple destructuring (M4): every misuse must be diagnosed.  */

#if defined test_arity_too_few
int f(void) { auto (a, b) = $(1, 2, 3); return 0; }

#elif defined test_arity_too_many
int f(void) { auto (a, b, c) = $(1, 2); return 0; }

#elif defined test_literal_arity
int f(void) { auto (a, b) = (1, 2, 3); return 0; }

#elif defined test_non_tuple
int f(void) { auto (a, b) = 1; return 0; }

#elif defined test_non_tuple_struct
struct S { int x; };
int f(void) { struct S s = { 1 }; auto (a, b) = s; return 0; }

#elif defined test_file_scope
auto (a, b) = $(1, 2);

#elif defined test_no_initializer
int f(void) { auto (a, b); return 0; }

#elif defined test_duplicate_names
int f(void) { auto (a, a) = $(1, 2); return 0; }

#elif defined test_loop_variable_shadowed
int f(void) { $foreach (n; $(1,)) int n = 2; return n; }

#elif defined test_pattern_not_name
int f(void) { auto (a, 1) = $(1, 2); return 0; }

#elif defined test_pattern_missing_name
int f(void) { auto (, b) = $(1, 2); return 0; }

#elif defined test_foreach_non_constant
int f(int x) { $foreach (n; x) n; return 0; }

#elif defined test_foreach_non_tuple
int f(void) { $foreach (n; 1) n; return 0; }

#elif defined test_foreach_float_element
int f(void) { $foreach (n; $(1.5, 2.5)) n; return 0; }

#elif defined test_foreach_nested_element
int f(void) { $foreach (n; $($(1, 2), 3)) n; return 0; }

#elif defined test_foreach_missing_name
int f(void) { $foreach (; $(1, 2)) ; return 0; }

#elif defined test_foreach_without_parens
int f(void) { $foreach n; $(1,) n; return 0; }

#elif defined test_manifest_arity
enum (a, b) = $(1, 2, 3);

#elif defined test_manifest_non_tuple
enum (a, b) = 1;

#elif defined test_manifest_duplicate_names
enum (a, a) = $(1, 2);

#elif defined test_manifest_no_initializer
enum (a, b);

#elif defined test_manifest_pattern_not_name
enum (a, 1) = $(1, 2);

#elif defined test_manifest_float_element
enum (a, b) = $(1.5, 2);
#endif
