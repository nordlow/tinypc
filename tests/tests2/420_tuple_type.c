#include <stdio.h>

typedef (int, char) Pair;
typedef ((int, char), double) Nested;

Pair p = {1, 'a'};
Nested n = {{2, 'b'}, 3.14};

int main(void) {
    printf("p[0] = %d, p[1] = %c\n", p[0], p[1]);
    printf("n[0][0] = %d, n[0][1] = %c, n[1] = %f\n", n[0][0], n[0][1], n[1]);
    printf("sizeof(Pair) = %lu\n", (unsigned long)sizeof(Pair));
    printf("_Alignof(Pair) = %lu\n", (unsigned long)_Alignof(Pair));
    
    Pair cp = (Pair){5, 'z'};
    printf("cp[0] = %d, cp[1] = %c\n", cp[0], cp[1]);

    int t_int = _Generic(p, Pair: 1, default: 0);
    printf("_Generic match: %d\n", t_int);

    printf("compatible: %d\n", __builtin_types_compatible_p(Pair, (int, char)));
    printf("incompatible: %d\n", __builtin_types_compatible_p(Pair, (int, int)));

    return 0;
}
