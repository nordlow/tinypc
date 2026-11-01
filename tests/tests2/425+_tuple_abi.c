/* Counterpart of 425_tuple_abi.c: same values, but with struct types.  */

struct s8 { int a; int b; };
struct s16 { long a; double b; };
struct s24 { long a; long b; long c; };
struct sness { struct s8 in; long z; };

struct s8 mk8(int a, int b) { struct s8 r = {a, b}; return r; }
struct s16 mk16(long a, double b) { struct s16 r = {a, b}; return r; }
struct s24 mk24(long a, long b, long c) { struct s24 r = {a, b, c}; return r; }

struct sness mknest(int a, int b, long z)
{
    struct sness r = {{a, b}, z};
    return r;
}

long sum8(struct s8 v) { return v.a + v.b; }
double sum16(struct s16 v) { return v.a + v.b; }
long sum24(struct s24 v) { return v.a + v.b + v.c; }
long sumnest(struct sness v) { return v.in.a * 100 + v.in.b * 10 + v.z; }

struct s8 twice8(struct s8 v) { v.a *= 2; v.b *= 3; return v; }
struct s24 rotate24(struct s24 v) { struct s24 r = {v.b, v.c, v.a}; return r; }
