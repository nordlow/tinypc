/* Tuple element access under bounds checking (-b).  */

#include <stdio.h>

typedef (int, char) Pair;
typedef ((int, char), double) Nested;

Pair g = {1, 'a'};
Nested gn = {{2, 'b'}, 3.5};

struct Box { Pair v; Nested n; };
struct Box gbox = {{4, 'd'}, {{5, 'e'}, 6.5}};

int main(void)
{
    Pair p = {7, 'f'};
    Pair arr[2] = {{8, 'g'}, {9, 'h'}};
    Nested n = {{10, 'i'}, 11.5};
    int i;

    printf("g %d %c\n", g[0], g[1]);
    printf("gn %d %c %g\n", gn[0][0], gn[0][1], gn[1]);
    printf("p %d %c\n", p[0], p[1]);
    printf("n %d %c %g\n", n[0][0], n[0][1], n[1]);
    printf("box %d %c %d %c %g\n", gbox.v[0], gbox.v[1],
           gbox.n[0][0], gbox.n[0][1], gbox.n[1]);

    for (i = 0; i < 2; i++)
        printf("arr %d %d %c\n", i, arr[i][0], arr[i][1]);

    p[0] = 12;
    n[1] = 13.5;
    arr[1][1] = 'j';
    printf("assign %d %d %c %g\n", p[0], arr[1][0], arr[1][1], n[1]);
    return 0;
}
