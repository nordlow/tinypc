/* UFCS syntax for `$members`: `T.$members` instead of `$members(T)`.  */

#include <stdio.h>

struct S { int a; double b; char c; };
typedef struct S SS;

enum Color { Red, Green = 5, Blue };

int main(void)
{
    $foreach (m; SS.$members) {
        printf("struct %s %d %d\n", $nameof(m), $offsetof(m), (int)sizeof(m));
    }
    $foreach (m; struct S.$members) {
        printf("tag %s %d\n", $nameof(m), $offsetof(m));
    }
    $foreach (m; enum Color.$members) {
        printf("enum %s %d %d\n", $nameof(m), $valueof(m), (int)sizeof(m));
    }
    return 0;
}
