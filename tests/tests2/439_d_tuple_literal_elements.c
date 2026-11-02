/* Tuple literal elements that are themselves tuples or arrays.  A
   tuple-typed element takes a nested tuple literal, and an array-typed
   element is copied into the literal's storage instead of being stored
   as a pointer.  */

#include <stdio.h>

typedef ((int, int), int) Nested;
typedef (int[3], int) Arr3;
typedef (char[4], int) Str;

struct S { int x, y; };
typedef (struct S[2], int) Structs;

static Nested mkn(void)
{
    Nested n;
    (int, int) in = (1, 2);
    n = (in, 3);
    return n;
}

static int sum_nested(Nested n)
{
    return n[0][0] + n[0][1] + n[1];
}

static Arr3 mka(void)
{
    int src[3] = {4, 5, 6};
    return (src, 7);
}

static int sum_arr(Arr3 t)
{
    return t[0][0] + t[0][1] + t[0][2] + t[1];
}

int main(void)
{
    /* tuple-typed elements: a run-time tuple literal, a tuple variable,
       function results in a copy and an argument position, and deeper
       nesting */
    Nested a = ((1, 2), 3);
    Nested b = mkn();
    (int, int) in = (7, 8);
    Nested c = (in, 9);
    (((int, int), int), int) d = (((1, 2), 3), 4);

    printf("nested %d %d %d\n", a[0][0], a[0][1], a[1]);
    printf("result %d %d %d sum %d\n", b[0][0], b[0][1], b[1], sum_nested(b));
    printf("var %d %d %d sum %d\n", c[0][0], c[0][1], c[1], sum_nested(c));
    printf("deep %d %d %d %d\n", d[0][0][0], d[0][0][1], d[0][1], d[1]);

    /* array-typed elements: an array variable, a compound literal, a
       string literal, and an array of structs, each of them as an
       initializer, a function result and an argument */
    int src[3] = {10, 20, 30};
    Arr3 e = (src, 1);
    Arr3 f = ((int[3]){40, 50, 60}, 2);
    Arr3 g = mka();
    Str h = ("abc", 3);
    struct S ss[2] = { {5, 6}, {7, 8} };
    Structs i = (ss, 11);
    ((int[2], int), int) j = (((int[2]){70, 80}, 4), 5);

    printf("array %d %d %d %d sum %d\n",
           e[0][0], e[0][1], e[0][2], e[1], sum_arr(e));
    printf("compound %d %d %d %d\n", f[0][0], f[0][1], f[0][2], f[1]);
    printf("return %d %d %d %d sum %d\n",
           g[0][0], g[0][1], g[0][2], g[1], sum_arr(g));
    printf("string %s %d %d\n", h[0], h[1], (int)sizeof h[0]);
    printf("structs %d %d %d %d %d\n",
           i[0][0].x, i[0][0].y, i[0][1].x, i[0][1].y, i[1]);
    printf("mixed %d %d %d %d\n", j[0][0][0], j[0][0][1], j[0][1], j[1]);

    /* an array element is a copy: changing the source or the
       destination must not affect the other */
    src[0] = 99;
    e[0][1] = 88;
    printf("copy %d %d %d %d\n", src[0], src[1], e[0][0], e[0][1]);

    /* destructuring a tuple with nested and array elements */
    auto (p, q) = a;
    auto (r, n) = e;
    printf("destructure %d %d %d %d %d %d\n",
           p[0], p[1], q, r[0], r[1], n);
    return 0;
}
