/* Comma expressions and '$' keep their meaning in D mode.  */

#include <stdio.h>

alias Pair = (int, char);

static int two(int a, int b) { return a * 10 + b; }
static int one(int a) { return a + 100; }

int main(void)
{
    int a = 1, b = 2, c = 3, i, j, s;
    int arr[5] = {10, 11, 12, 13, 14};
    Pair p = {4, 'd'};
    auto sl = arr[1..4];

    static assert(sizeof(Pair) == 8);
    $assert(sizeof((int, char)) == 8);

    printf("init %d\n", (a, b));
    printf("stmt %d\n", (a = 4, b));
    printf("one %d\n", one((a, b)));
    printf("two %d\n", two(a, b));
    printf("add %d\n", 5 + (a, b));
    printf("cond %d\n", (a, b) ? 7 : 8);
    printf("typeof %d\n", (int)sizeof(typeof((a, b))));
    printf("nested %d\n", one((a, (b, c))));

    s = 0;
    for (i = 0, j = 0; i < 3; i++, j += 2)
        s += i + j;
    printf("for %d %d %d\n", s, i, j);

    printf("tuple %d %c\n", p[0], p[1]);

    /* opDollar */
    printf("dollar %d %d %d\n", arr[$-1], arr[$/2], sl[$-1]);
    printf("slice %d %d %d\n", (int)sl.length, sl.ptr[0], sl.ptr[2]);
    printf("range %d %d\n", (int)sl[0..$].length, sl[0..$].ptr[2]);
    printf("index %d %d\n", arr[sizeof((int, char)) - 6], arr[(a, b)]);

    {
        int n = 4;
        int vla[n];
        for (i = 0; i < n; i++)
            vla[i] = i * 7;
        printf("vla %d %d\n", vla[$-1], vla[0]);
    }

    switch ((a, b)) {
    case 2:
        printf("switch 2\n");
        break;
    default:
        printf("switch other\n");
    }
    return 0;
}
