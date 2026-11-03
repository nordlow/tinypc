/* Tuple destructuring assignment in D mode: compile-time `$(...)`
   tuples and aliases work on the right side, and comma expressions keep
   their D meaning.  */

#include <stdio.h>

alias Pair = (int, int);

static (int, int) make(int a, int b) { return (a, b); }

int main(void)
{
    int a = 1, b = 2;

    (a, b) = (b, a);
    printf("swap %d %d\n", a, b);

    (a, b) = make(5, 6);
    printf("call %d %d\n", a, b);

    (a, b) = $(2, 1);
    printf("const %d %d\n", a, b);

    Pair p = (3, 4);
    (a, b) = p;
    printf("alias %d %d\n", a, b);

    {
        int arr[3] = {10, 11, 12};
        (a, b) = (arr[1], arr[2]);
        printf("index %d %d\n", a, b);
    }

    /* `(a, b)` without '=' stays a comma expression */
    printf("comma %d %d\n", (a = 7, b), b);
    return 0;
}
