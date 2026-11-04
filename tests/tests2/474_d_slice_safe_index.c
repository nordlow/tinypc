/* Indices of the form $/k are below the length of every non-empty
   slice, so only the non-empty precondition is checked.  An empty slice
   must still trap instead of being silently accepted.  */

#include <stdio.h>

typedef int[..] Ints;

int main(void)
{
    int a[6] = {10, 11, 12, 13, 14, 15};
    Ints s = a[..];
    Ints empty = a[0..0];

    printf("safe %d %d %d %d %d\n",
           s[$ / 2], s[$ / 3], s[$ / 4], s[$ / 6], s[$ / (2)]);
    printf("constant %d\n", s[0]);
    printf("empty\n");
    printf("empty value %d\n", empty[$ / 2]);
    return 0;
}
