#include <stdio.h>

int main() {
    _Complex double z1;
    __real__ z1 = 1.0;
    __imag__ z1 = 2.0;

    _Complex double z2;
    __real__ z2 = 1.0;
    __imag__ z2 = 2.0;

    _Complex double z3;
    __real__ z3 = 3.0;
    __imag__ z3 = 4.0;

    if ((__real__ z1 == __real__ z2) && (__imag__ z1 == __imag__ z2)) printf("z1 == z2\n");
    if ((__real__ z1 != __real__ z3) || (__imag__ z1 != __imag__ z3)) printf("z1 != z3\n");
    return 0;
}
