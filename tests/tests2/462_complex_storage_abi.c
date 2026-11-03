#include <stdio.h>

_Complex double test_spill(_Complex double z) {
    _Complex double a;
    __real__ a = -__imag__ z;
    __imag__ a = __real__ z;
    return a;
}

_Complex double add_complex(_Complex double a, _Complex double b) {
    _Complex double res;
    __real__ res = __real__ a + __real__ b;
    __imag__ res = __imag__ a + __imag__ b;
    return res;
}

int main() {
    _Complex double z;
    __real__ z = 3.0;
    __imag__ z = 4.0;
    _Complex double res1 = test_spill(z);
    printf("res: %.1f + %.1fi\n", __real__ res1, __imag__ res1);

    _Complex double z1;
    __real__ z1 = 1.0;
    __imag__ z1 = 2.0;

    _Complex double z2;
    __real__ z2 = 3.0;
    __imag__ z2 = 4.0;

    _Complex double res2 = add_complex(z1, z2);
    printf("abi res: %.1f + %.1fi\n", __real__ res2, __imag__ res2);
    return 0;
}
