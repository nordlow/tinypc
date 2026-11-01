/* Tuple ABI must equal the ABI of the equivalent struct.  The callee is
   compiled separately (425+_tuple_abi.c) and uses struct types
   of the same layout, so the caller's tuple types are only compatible
   with them if the ABI matches.  */

#include <stdio.h>

(int, int) mk8(int, int);
(long, double) mk16(long, double);
(long, long, long) mk24(long, long, long);
((int, int), long) mknest(int, int, long);

long sum8((int, int));
double sum16((long, double));
long sum24((long, long, long));
long sumnest(((int, int), long));

(int, int) twice8((int, int));
(long, long, long) rotate24((long, long, long));

int main(void)
{
    (int, int) a = mk8(3, 4);
    (long, double) b = mk16(5, 6.5);
    (long, long, long) c = mk24(7, 8, 9);
    ((int, int), long) d = mknest(1, 2, 3);
    (int, int) e = twice8(a);
    (long, long, long) f = rotate24(c);

    printf("8 %d %d\n", a[0], a[1]);
    printf("16 %ld %.1f\n", b[0], b[1]);
    printf("24 %ld %ld %ld\n", c[0], c[1], c[2]);
    printf("nested %d %d %ld\n", d[0][0], d[0][1], d[1]);
    printf("sum %ld %.1f %ld %ld\n",
           sum8(a), sum16(b), sum24(c), sumnest(d));
    printf("twice %d %d\n", e[0], e[1]);
    printf("rotate %ld %ld %ld\n", f[0], f[1], f[2]);
    printf("layout %d %d %d %d\n",
           (int)sizeof((int, int)), (int)sizeof((long, double)),
           (int)sizeof((long, long, long)), (int)sizeof(((int, int), long)));
    return 0;
}
