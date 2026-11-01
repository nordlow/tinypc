/* Comma expressions must keep their meaning in C mode.  */

#include <stdio.h>

typedef int T;

static int two(int a, int b) { return a * 10 + b; }
static int one(int a) { return a + 100; }
static int ret_comma(int a, int b) { return (a, b); }

struct S { int a; char b; };

int main(void)
{
    int a = 1, b = 2, c = 3, i, j, s;

    printf("init %d\n", (a, b));
    printf("stmt %d\n", (a = 4, b));
    printf("one %d\n", one((a, b)));
    printf("two %d\n", two(a, b));
    printf("add %d\n", 5 + (a, b));
    printf("cast %d\n", (int)(a, b));
    printf("castel %d\n", ((int)a, (int)b));
    printf("cond %d\n", (a, b) ? 7 : 8);
    printf("sizeof %d\n", (int)sizeof(__typeof__(((void)0, (T){3}))));
    printf("typeof %d\n", (int)sizeof(__typeof__((a, b))));
    printf("generic %d\n", _Generic((a, b), int: 1, default: 0));
    printf("nested %d\n", one((a, (b, c))));
    printf("ret %d\n", ret_comma(a, b));

    s = 0;
    for (i = 0, j = 0; i < 3; i++, j += 2)
        s += i + j;
    printf("for %d %d %d\n", s, i, j);

    {
        int arr[3] = {10, 11, 12};
        printf("subscript %d\n", arr[(a, b)]);
    }

    {
        struct S v = {5, 'x'};
        printf("field %d\n", (v.a, v.b));
    }

    switch ((a, b)) {
    case 2:
        printf("switch 2\n");
        break;
    default:
        printf("switch other\n");
    }

    while ((a, b), a < 6)
        ++a;
    printf("while %d\n", a);
    return 0;
}
