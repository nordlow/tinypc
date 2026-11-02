/* UFCS syntax for `$members`: `T.$members` instead of `$members(T)`.  */

#include <stdio.h>

struct S { int a; double b; char c; };
typedef struct S SS;

enum Color { Red, Green = 5, Blue };

int main(void)
{
    $foreach (m; SS.$members) {
        printf("struct %s %d %d\n", m.$nameof, m.$offsetof, (int)m.sizeof);
        typeof(m) x = 0;
        (void)x;
    }
    $foreach (m; struct S.$members) {
        printf("tag %s %d\n", m.$nameof, m.$offsetof);
    }
    $foreach (m; enum Color.$members) {
        printf("enum %s %d %d\n", m.$nameof, m.$valueof, (int)m.sizeof);
    }
    return 0;
}
