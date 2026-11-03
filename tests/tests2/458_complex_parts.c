#include <stdio.h>

int main() {
    _Complex double z;
    __real__ z = 3.0;
    __imag__ z = 4.0;
    double r = __real__ z;
    double i = __imag__ z;
    printf("real: %.1f, imag: %.1f\n", r, i);

    __real__ z = 5.0;
    __imag__ z = 12.0;
    printf("modified real: %.1f, imag: %.1f\n", __real__ z, __imag__ z);
    return 0;
}
