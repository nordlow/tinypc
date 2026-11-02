/* Manifest-constant destructuring (M5): `enum (a, b) = $(1, 2)` binds
   the elements of a compile-time tuple as integer manifest constants,
   without storage and without code at the binding site.  */

#include <stdio.h>

enum (limit, step) = $(4, 2);
enum (one,) = $(1,);

static int table[limit];
$assert(limit == 4 && step == 2 && one == 1);
static assert(limit * step == 8);

int main(void)
{
    enum (rows, cols,) = $(3, 5);   /* trailing comma */
    enum (next,) = $(step + 1,);
    int grid[rows][cols];
    int sum = 0;

    $if (rows == 3 && cols == 5)
        sum += 1;

    /* the names are ordinary integer constant expressions */
    switch (sum) {
    case 1: sum = 10; break;
    default: sum = -1;
    }

    /* usable in a `$foreach` tuple and as local array bounds */
    $foreach (k; $(one, limit, next)) {
        int local[k];
        sum += k * (int)sizeof local;
    }

    /* a nested block may shadow them */
    {
        enum (limit, step) = $(1, 1);
        sum += limit + step;
    }

    printf("global %d %d %d %d\n", limit, step, one, (int)sizeof table);
    printf("local %d %d %d %d\n", rows, cols, next, (int)sizeof grid);
    printf("sum %d\n", sum);
    return 0;
}
