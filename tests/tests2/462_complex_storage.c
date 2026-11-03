#include <stdio.h>

_Complex double test_spill(_Complex double z) {
    _Complex double a;
    __real__ a = -__imag__ z;
    __imag__ a = __real__ z;
    return a;
}

int main() {
    _Complex double z;
    __real__ z = 3.0;
    __imag__ z = 4.0;
    _Complex double res;
    res = test_spill(z);
    double r, i;
    r = __real__ res;
    i = __imag__ res;
    printf("res: %.1f + %.1fi\n", r, i);
    return 0;
}
