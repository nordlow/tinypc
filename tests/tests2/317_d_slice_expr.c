#include <stdio.h>

typedef int IntSlice[..];
typedef int Row[3];
typedef Row RowSlice[..];

int garr[5] = { 10, 11, 12, 13, 14 };

IntSlice gfull(void)
{
    return garr[..];
}

int sum(IntSlice s)
{
    int i, r = 0;
    for (i = 0; i < (int)s.length; i++)
        r += s.ptr[i];
    return r;
}

int main(void)
{
    int a[5] = { 0, 1, 2, 3, 4 };
    IntSlice s = a[1..4];
    IntSlice all = a[..];
    IntSlice ps = (a + 1)[0..3];
    IntSlice ss = s[1..3];
    IntSlice cp = s[..];
    IntSlice g = gfull();
    int m[2][3] = { { 1, 2, 3 }, { 4, 5, 6 } };
    IntSlice row = m[1][0..3];
    RowSlice rows = m[..];
    int n = 3;
    int vla[n];
    IntSlice vs;
    int i;

    printf("%ld %d %d %d\n", (long)s.length, s.ptr[0], s.ptr[1], s.ptr[2]);
    printf("%ld %d %d\n", (long)all.length, all.ptr[0], all.ptr[4]);
    printf("%ld %d %d\n", (long)ps.length, ps.ptr[0], ps.ptr[2]);
    printf("%ld %d %d\n", (long)ss.length, ss.ptr[0], ss.ptr[1]);
    printf("%d %ld %d\n", sum(a[2..5]), (long)cp.length, cp.ptr[0]);
    printf("%ld %d %d\n", (long)g.length, g.ptr[0], g.ptr[4]);
    printf("%ld %d %d %ld %d %d\n", (long)row.length, row.ptr[0], row.ptr[2],
           (long)rows.length, rows.ptr[1][0], rows.ptr[1][2]);
    printf("%ld %d\n", (long)a[3..5].length, a[3..5].ptr[1]);

    for (i = 0; i < n; i++)
        vla[i] = i + 1;
    vs = vla[..];
    printf("%ld %d %d\n", (long)vs.length, vs.ptr[0], vs.ptr[2]);

    /* slicing a slice copies the header, it does not alias it */
    cp.length = 0;
    printf("%ld %ld\n", (long)s.length, (long)cp.length);

    /* missing bounds: V[L..] and V[..H] */
    {
        IntSlice sl = a[1..];
        IntSlice sh = a[..3];
        printf("%ld %d %d\n", (long)sl.length, sl.ptr[0], sl.ptr[3]);
        printf("%ld %d %d\n", (long)sh.length, sh.ptr[0], sh.ptr[2]);
    }

    /* the '...' range spelling is accepted as well */
    {
        IntSlice d = a[1...4];
        printf("%ld %d %d\n", (long)d.length, d.ptr[0], d.ptr[2]);
    }
    return 0;
 }
