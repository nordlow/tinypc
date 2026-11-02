/* Compile-time reflection: `$members(T)` returns a tuple of the field types
   of a struct/union, usable with `$foreach`, `sizeof` and `typeof`.  */

#include <stdio.h>

struct S { int a; double b; char c; };

int main(void)
{
    $foreach (T; $members(struct S)) {
        printf("size %d\n", (int)sizeof(T));
    }

    /* typeof on an element works via the loop variable's type */
    $foreach (T; $members(struct S)) {
        typeof(T) x = 0;
        (void)x;
    }
    printf("ok\n");
    return 0;
}
