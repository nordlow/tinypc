#include <stdio.h>

typedef int[..] Ints;

int main(void)
{
    int a[5] = {10, 11, 12, 13, 14};
    Ints s = a[1..4];

    printf("before\n");
    printf("%d\n", s[50]);
    printf("unreachable\n");
    return 0;
}
