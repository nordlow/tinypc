#include <stdio.h>

typedef int[..] Ints;

int main(void)
{
    int a[64];
    int i;

    for (i = 0; i < 64; i++)
        a[i] = i * 10;
    Ints s = a[1..4];   /* length 3, but a[51] is still valid memory */

    printf("inside %d\n", s[1]);
    printf("outside %d\n", s[50]);
    printf("slice %ld\n", (long)s[0..6].length);
    return 0;
}
