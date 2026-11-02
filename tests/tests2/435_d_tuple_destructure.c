/* Tuple destructuring (M4): `auto (a, b) = expr;` binds the elements of
   a tuple initializer, which is evaluated exactly once, and
   `$foreach (x; t) ...` replays its body once per element of a
   compile-time tuple.  */

#include <stdio.h>

struct S { int x, y; };

static int calls;
static (int, int) make(int a, int b)
{
    ++calls;
    return (a, b);
}
static (int, double) mixed(void) { return (3, 4.5); }
static (struct S, int) mks(void)
{
    struct S s = { 1, 2 };
    return (s, 3);
}
static (int, int, int, int, int) five(void) { return (1, 2, 3, 4, 5); }

/* a file-scope `$foreach` replays declarations into the global scope */
$foreach (n; $(7,))
    static int file_loop = n;

int main(void)
{
    /* a function result, evaluated once */
    auto (a, b) = make(3, 4);
    printf("call %d %d %d\n", a, b, calls);

    /* a tuple variable: the pattern aliases a copy, so assigning to a
       name does not modify the source tuple */
    (int, int) t = (5, 6);
    auto (c, d) = t;
    c = 50;
    d = t[1] + 1;
    printf("copy %d %d %d %d\n", c, d, t[0], t[1]);

    /* a compile-time constant tuple */
    auto (e, f) = $(7, 8);
    printf("const %d %d\n", e, f);

    /* a run-time tuple literal */
    auto (g, h) = (9, 10);
    printf("literal %d %d\n", g, h);

    /* mixed element types */
    auto (m, x) = mixed();
    printf("mixed %d %.1f\n", m, x);

    /* one-element patterns */
    auto (only,) = $(11,);
    auto (single,) = (12,);
    printf("single %d %d\n", only, single);

    /* a parenthesized declarator without a comma is not a pattern */
    auto (plain) = 21;
    printf("plain %d\n", plain);

    /* a trailing comma and several patterns in one declaration */
    auto (j, k,) = $(1, 2);
    auto (l, o) = $(3, 4), (u, w) = $(5, 6);
    auto (y, z) = (7, 8), extra = 9;
    printf("multi %d %d %d\n", j + l + u + y, k + o + w + z, extra);

    /* an aggregate element and a tuple wider than a register */
    auto (s, n) = mks();
    auto (i1, i2, i3, i4, i5) = five();
    printf("struct %d %d %d\n", s.x, s.y, n);
    printf("wide %d\n", i1 + i2 + i3 + i4 + i5);

    /* arbitrary expressions as elements */
    auto (p, q) = (s.x * 10, n + i5);
    printf("expr %d %d\n", p, q);

    /* `$foreach' variables are integer manifest constants, usable as
       array bounds */
    {
        int sum = 0;
        $foreach (k; $(1, 2, 3)) {
            int arr[k];
            sum += k * (int)sizeof arr;
        }
        printf("foreach %d\n", sum);
    }

    /* braceless and nested bodies */
    {
        int total = 0;
        $foreach (i; $(1, 2))
            $foreach (j; $(10, 20))
                total += i * j;
        printf("nested %d\n", total);
    }

    /* every iteration opens its own scope: the loop variable shadows an
       enclosing one and neither the variable nor other declarations
       escape the iteration */
    {
        int n = 42;
        int sum = 0;
        $foreach (n; $(5, 6)) {
            int twice = n * 2;
            { int n = 3; sum += n; }   /* shadows the loop variable */
            sum += twice;
        }
        printf("scope %d %d\n", n, sum);
    }

    /* a skipped `$if' branch may contain an invalid body */
    $if (0)
        $foreach (bad; $(1, 2)) { this is not valid C }
    else
        printf("skipped\n");

    printf("file %d\n", file_loop);
    return 0;
}
