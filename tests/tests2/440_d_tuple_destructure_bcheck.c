/* Tuple destructuring and tuple literals under bounds checking (-b).

   The compiler allocates several hidden temporaries for a tuple
   initializer and for struct returns; every one of them must be
   registered with the bound checker, or the element copies trip
   `__bound_memmove` with an "invalid pointer".  */

#include <stdio.h>

struct S { int x, y; };

static (int, int) make(int a, int b) { return (a, b); }
static (int, double) mixed(void) { return (3, 4.5); }
static (struct S, int) mks(void)
{
    struct S s = { 1, 2 };
    return (s, 3);
}
static (int, int, int, int, int) five(void)
{
    return (1, 2, 3, 4, 5);
}

int main(void)
{
    /* a tuple returned in integer registers */
    auto (a, b) = make(3, 4);
    printf("call %d %d\n", a, b);

    /* a mixed int/double tuple: the register values are first spilled
       to a hidden stack slot */
    auto (m, x) = mixed();
    printf("mixed %d %.1f\n", m, x);

    /* an aggregate element: returned through a hidden slot */
    auto (s, n) = mks();
    printf("struct %d %d %d\n", s.x, s.y, n);

    /* a tuple wider than the return registers */
    auto (i1, i2, i3, i4, i5) = five();
    printf("wide %d\n", i1 + i2 + i3 + i4 + i5);

    /* destructuring a tuple variable aliases a copy */
    (int, int) t = (5, 6);
    auto (c, d) = t;
    c = 50;
    printf("copy %d %d %d %d\n", c, d, t[0], t[1]);

    /* a run-time tuple literal initializer */
    auto (g, h) = (7, 8);
    printf("literal %d %d\n", g, h);

    /* a nested tuple variable */
    ((int, int), int) nt = ((9, 10), 11);
    auto (p, q) = nt;
    printf("nested %d %d %d\n", p[0], p[1], q);

    /* a compile-time tuple still works under -b */
    auto (e, f) = $(12, 13);
    printf("const %d %d\n", e, f);

    return 0;
}
