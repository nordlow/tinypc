/* `$members` reflection: name, offset and size of each field.  */

#include <stdio.h>

struct S { int a; double b; char c; };

int main(void)
{
    $foreach (m; $members(struct S)) {
        printf("%s %d %d\n", $nameof(m), $offsetof(m), (int)sizeof(m));
    }
    return 0;
}
