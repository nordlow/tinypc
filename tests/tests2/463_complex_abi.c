#include <stdio.h>

_Complex double add_complex(_Complex double a, _Complex double b) {
    _Complex double res;
    __real__ res = __real__ a + __real__ b;
    __imag__ res = __imag__ a + __imag__ b;
    return res;
}

int main() {
    _Complex double z1;
    __real__ z1 = 1.0;
    __imag__ z1 = 2.0;

    _Complex double z2;
    __real__ z2 = 3.0;
    __imag__ z2 = 4.0;

    _Complex double res = add_complex(z1, z2);
    double r = __real__ res;
    double i = __imag__ res;
    printf("abi res: %.1f + %.1fi\n", r, i);
    return 0;
}
