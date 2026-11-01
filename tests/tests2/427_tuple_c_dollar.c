/* '$' stays an identifier character in C mode, and type tuples work in
   C mode as well.  */

#include <stdio.h>

int $(int x) { return x + 1; }
int $sum(int a, int b) { return a + b; }
int $x = 3;
int $if = 4;
int $tuple(int x) { return x * 2; }

typedef (int, char) Pair;

int main(void)
{
    int $(int);
    int $sum(int, int);
    Pair p = {5, 'p'};

    printf("$ %d\n", $(1));
    printf("sum %d\n", $sum($x, $if));
    printf("idents %d %d\n", $x, $if);
    printf("tuple %d %c\n", p[0], p[1]);
    printf("dollar_call %d\n", $tuple(p[0]));
    printf("size %d\n", (int)sizeof((int, char)));
    return 0;
}
