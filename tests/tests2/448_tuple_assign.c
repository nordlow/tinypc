/* Tuple destructuring assignment: `(a, b) = (b, a);` stores the right
   side into every destination.  The right side is evaluated into a
   temporary first, so a swap really swaps.  A group without a top-level
   comma, or one that is not followed by '=', stays a comma expression.  */

#include <stdio.h>

struct S { int x; char tag[4]; };

static int calls;
static (int, int) make(int a, int b)
{
    ++calls;
    return (a, b);
}
static (int, int, int) three(void) { return (7, 8, 9); }

int main(void)
{
    int a = 1, b = 2, c = 3, one = 0;

    /* a swap */
    (a, b) = (b, a);
    printf("swap %d %d\n", a, b);

    /* a three-element rotation */
    (a, b, c) = (b, c, a);
    printf("rotate %d %d %d\n", a, b, c);

    /* an element pattern and a parenthesized lvalue */
    (one,) = (42,);
    printf("one %d\n", one);
    ((a), (b)) = (b, a);
    printf("paren %d %d\n", a, b);

    /* the right side is evaluated exactly once */
    calls = 0;
    (a, b) = make(3, 4);
    (a, b, c) = three();
    printf("call %d %d %d %d\n", a, b, c, calls);

    /* tuple variables, including a mixed-type conversion */
    {
        (int, int) t = (5, 6);
        (long, double) m = (7, 8);
        long l;
        double d;
        (a, b) = t;
        (l, d) = m;
        printf("var %d %d %ld %.1f\n", a, b, l, d);
    }

    /* element conversions */
    {
        int i = 0;
        double d = 0;
        (i, d) = (65.9, 7);
        printf("conv %d %.1f\n", i, d);
    }

    /* struct fields, array elements and dereferenced pointers */
    {
        struct S s = {0, "abc"};
        int arr[3] = {0, 0, 0};
        int x = 10, y = 20, *px = &x, *py = &y;

        (s.x, arr[1]) = (1, 2);
        (*px, *py) = (*py, *px);
        printf("mem %d %d %s %d %d\n", s.x, arr[1], s.tag, x, y);
    }

    /* array and struct destinations */
    {
        char s1[4] = "abc", s2[4] = "xyz";
        struct S p = {1, "one"}, q = {2, "two"};
        (s1, one) = (s2, 9);
        (p, one) = (q, 10);
        printf("agg %s %d %d %s %d\n", s1, one, p.x, p.tag, one - 9);
    }

    /* a nested tuple destination */
    {
        (int, int) t = (0, 0);
        (t, one) = ((13, 14), 15);
        printf("nested %d %d %d\n", t[0], t[1], one);
    }

    /* the value of the assignment expression is the right side */
    a = b = 0;
    c = ((a, b) = (3, 4))[1];
    printf("value %d %d %d\n", a, b, c);

    /* chained destructuring assignments */
    {
        int d, e;
        (a, b) = (d, e) = (20, 21);
        printf("chain %d %d %d %d\n", a, b, d, e);
    }

    /* in conditions and loops */
    a = b = 0;
    if (((a, b) = (1, 2))[1] == 2)
        printf("if %d %d\n", a, b);
    {
        int i;
        for (i = 0; i < 2; ++i)
            (a, b) = (b, a);
        printf("loop %d %d\n", a, b);
    }

    /* a group without a comma is just a parenthesized lvalue */
    (a) = 30;
    printf("plain %d\n", a);

    /* a group that is not a pattern keeps its C meaning: the comma
       expression yields the last operand, which is what is assigned */
    a = 5;
    (a + 1, b) = (10, 11);
    printf("nonpattern %d %d\n", a, b);

    /* comma expressions keep their meaning: no top-level comma before
       '=', or a token other than '=' after the group */
    a = 1;
    b = 2;
    c = (a, b);
    printf("comma %d %d\n", c, (a = 7, b));
    printf("args %d %d\n", (a, b), a);
    printf("cond %d\n", (a, b) ? 1 : 0);

    return 0;
}
