/* Compile-time value tuples (P2): $(...) constants in constant
   contexts, constant folding of a tuple index, constant tuple
   initializers and $if.  */

#include <stdio.h>

enum N = $(1, 2)[0];
enum M = $(4, 8)[1];

$assert($(4, 8)[1] == 8);
static assert($(4, 8)[0] == 4);

static int table[$(4, 8)[1]];
static int table3[$(1, 2, 3)[2] + $(5,)[0]];

static const int k = $(2 + 3);
static const int m = $(7,)[0];
static const double d = $(1.5, 2.5)[1];

typedef (int, int) Pair;
static Pair gp = $(1, 2);
static (int, double) gmixed = $(3, 2.5);

#define TWO $(1, 2)

$if (1)
    static int gv = 5;
else
    static int gv = 6;

$if (0)
    static int gw = 7;
else
    static int gw = 8;

int main(void)
{
    Pair p = $(9, 10);
    (int, int) nested = $($(20, 21)[0], 22);
    int idx = $(1, 2)[1];
    int a[4];
    int b[5] = { 0, 1, 2, 3, 4 };

    a[$(0, 3)[1]] = 42;

    printf("enums %d %d\n", N, M);
    printf("arrays %d %d\n", (int)sizeof(table), (int)sizeof(table3));
    printf("scalars %d %d %.1f\n", k, m, d);
    printf("static %d %d %.1f\n", gp[0], gp[1], gmixed[1]);
    printf("local %d %d %d %d\n", p[0], p[1], nested[0], nested[1]);
    printf("fold %d %d %d %d\n", $(30, 31)[1], idx, $(7, 8)[0], TWO[1]);
    printf("subscript %d\n", a[3]);
    printf("length %d %d\n", b[$-1], b[$(2, 3)[1]]);
    printf("sizeof %d %d\n", (int)sizeof($(1, 2)), (int)sizeof((int, double)));

    $if ($(1, 2)[0] == 1) {
        printf("if-taken\n");
    } else {
        this branch is not valid C (
    }
    $if (0) {
        printf("if-dead\n");
    } else {
        printf("else-taken\n");
    }
    $if (1) {
        $if (0) printf("nested-dead\n");
        else printf("nested-taken\n");
    }
    printf("scope %d %d\n", gv, gw);

    /* `$if` opens no scope of its own: a braceless branch declares
       into the enclosing scope, while braces keep their ordinary
       meaning.  */
    $if (1) int sx = 11;
    $if (0) int sx_dead = 12;
    $if (0) int sy = 13; else int sy = 14;
    $if (1) $if (1) int sz = 15;
    {
        int shadow = 1;
        $if (1) {
            int shadow = 2;
            printf("shadow %d\n", shadow);
        }
        printf("decls %d %d %d %d\n", sx, sy, sz, shadow);
    }
    printf("outer %d\n", sx + sy + sz);

    /* a manifest constant may be shadowed by a nested block */
    enum L = 7;
    {
        int L = 3;
        printf("manifest inner %d\n", L);
    }
    printf("manifest outer %d\n", L);
    return 0;
}
