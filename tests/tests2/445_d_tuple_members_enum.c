/* `$members` reflection over an enum type.  */

#include <stdio.h>

enum Color { Red, Green = 5, Blue };

int main(void)
{
    $foreach (m; $members(enum Color)) {
        printf("%s %d %d\n", $nameof(m), $valueof(m), (int)sizeof(m));
    }
    return 0;
}
