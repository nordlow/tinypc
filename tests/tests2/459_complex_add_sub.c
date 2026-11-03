#include <stdio.h>

int main() {
    _Complex double z1;
    __real__ z1 = 1.0;
    __imag__ z1 = 2.0;

    _Complex double z2;
    __real__ z2 = 3.0;
    __imag__ z2 = 4.0;

    _Complex double sum;
    __real__ sum = __real__ z1 + __real__ z2;
    __imag__ sum = __imag__ z1 + __imag__ z2;

    _Complex double diff;
    __real__ diff = __real__ z2 - __real__ z1;
    __imag__ diff = __imag__ z2 - __imag__ z1;

    printf("sum: %.1f + %.1fi\n", __real__ sum, __imag__ sum);
    printf("diff: %.1f + %.1fi\n", __real__ diff, __imag__ diff);
    return 0;
}
