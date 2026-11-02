/* Type-level tuple operations (M5): `typeof` on tuple elements and
   tuple types, structural type identity, and qualifier propagation
   from a tuple to its elements.  */

#include <stdio.h>

typedef (int, char) Pair;
typedef (int, char) SamePair;
typedef ((int, int), long) Nested;

static Pair mk(void) { return (7, 'z'); }
static Nested mkn(void)
{
    Nested n;
    (int, int) in = (1, 2);
    n = (in, 3L);
    return n;
}

int main(void)
{
    Pair p = (1, 'a');
    SamePair q = (2, 'b');
    Nested n = mkn();
    typeof(p[0]) x = 10;
    typeof(p) r = mk();
    typeof(n[0]) inner = n[0];
    typeof((int, char)) t2;
    (const int, int) c;
    (int, int) plain = (5, 6);
    (const (int, int), int) ct;

    printf("typeof %d %d %d %d %d\n", x, r[0], (int)r[1], inner[0], inner[1]);
    printf("sizes %d %d %d\n", (int)sizeof(typeof(p[0])), (int)sizeof(t2),
           (int)sizeof(typeof(n[0])));
    printf("align %d %d %d\n", (int)_Alignof(Pair), (int)_Alignof(typeof(n[0])),
           (int)_Alignof((char, double)));

    /* tuple identity is structural: two spellings of the same element
       types are the same type, a different order is not */
    printf("same %d %d %d\n", __builtin_types_compatible_p(Pair, SamePair),
           __builtin_types_compatible_p(typeof(p), (int, char)),
           __builtin_types_compatible_p(typeof(p), (char, int)));
    printf("generic %d %d %d\n", _Generic(q, Pair: 1, default: 0),
           _Generic(n, Nested: 1, default: 0),
           _Generic(p[1], char: 1, int: 2, default: 0));

    /* a const tuple makes its non-array elements const, as a const
       struct makes its members const */
    printf("const %d %d %d %d\n",
           _Generic(&c[0], const int *: 1, int *: 2, default: 0),
           _Generic(&c[1], const int *: 1, int *: 2, default: 0),
           _Generic(&plain[0], const int *: 1, int *: 2, default: 0),
           _Generic(&plain[1], const int *: 1, int *: 2, default: 0));
    printf("elem %d %d\n",
           _Generic(&ct[0], const (int, int) *: 1, (int, int) *: 2, default: 0),
           _Generic(&n[0], const (int, int) *: 1, (int, int) *: 2, default: 0));

    /* a tuple-typed element keeps its type through typeof and is a
       copy when used to initialize a variable */
    typeof(n[0]) copy = n[0];
    copy[0] = 40;
    printf("copy %d %d %d\n", copy[0], copy[1], n[0][0]);
    return 0;
}
