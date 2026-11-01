#if defined test_empty_tuple
typedef () Empty;

#elif defined test_void_element
(void, int) bad;

#elif defined test_function_element
typedef (int (void), char) Bad;

#elif defined test_incomplete_element
struct Incomplete;
(struct Incomplete, int) bad;

#elif defined test_storage_element
typedef (int, static char) Bad;

#elif defined test_vla_element
void f(int n) { typedef (int[n], char) Bad; }

#elif defined test_index_out_of_range
typedef (int, char) Pair;
int f(Pair p) { return p[2]; }

#elif defined test_index_not_constant
typedef (int, char) Pair;
int f(Pair p, int i) { return p[i]; }

#elif defined test_cast_to_tuple
typedef (int, char) Pair;
Pair f(void) { return (Pair)0; }

#elif defined test_tuple_arithmetic
typedef (int, char) Pair;
int f(Pair a, Pair b) { return (a + b)[0]; }
#endif
