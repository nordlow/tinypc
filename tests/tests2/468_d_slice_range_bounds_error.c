#include <stdio.h>

typedef int[..] Ints;

int main(void)
{
    int a[5] = {10, 11, 12, 13, 14};
    Ints s = a[1..4];

    printf("before\n");
    printf("%ld\n", (long)s[0..6].length);
    printf("unreachable\n");
    return 0;
}
