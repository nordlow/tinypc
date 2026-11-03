#include <stdio.h>

typedef int[..] Ints;

int main(void)
{
    int a[5] = {10, 11, 12, 13, 14};
    Ints s = a[1..4];
    int i = -1;

    printf("before\n");
    printf("%d\n", s[i]);
    printf("unreachable\n");
    return 0;
}
