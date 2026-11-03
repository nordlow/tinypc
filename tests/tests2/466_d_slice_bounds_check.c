#include <stdio.h>

typedef int[..] Ints;

int main(void)
{
    int a[5] = {10, 11, 12, 13, 14};
    Ints s = a[1..4];   /* length 3: a[1], a[2], a[3] */

    printf("index %d %d %d\n", s[0], s[1], s[2]);
    printf("dollar %d %d\n", s[$ - 1], s[$ / 2]);
    printf("full %d %ld\n", s[0..$].ptr[0], (long)s[0..$].length);
    printf("tail %d %ld\n", s[$ - 2..$].ptr[0], (long)s[$ - 2..$].length);
    printf("inclusive %d %ld\n", s[0..=2].ptr[2], (long)s[0..=2].length);
    printf("empty %ld %ld\n", (long)s[2..2].length, (long)s[3..3].length);

    /* several checked expressions in one argument list */
    printf("multi %d %d\n", s[0], s[2]);

    s[1] += 10;
    s[0..2] = 7;
    printf("store %d %d %d %d\n", a[1], a[2], a[3], a[4]);
    return 0;
}
