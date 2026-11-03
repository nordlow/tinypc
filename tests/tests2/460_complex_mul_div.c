#include <stdio.h>

int main() {
    _Complex double z1;
    __real__ z1 = 1.0;
    __imag__ z1 = 2.0;

    _Complex double z2;
    __real__ z2 = 3.0;
    __imag__ z2 = 4.0;

    double r_prod = (__real__ z1 * __real__ z2) - (__imag__ z1 * __imag__ z2);
    double i_prod = (__real__ z1 * __imag__ z2) + (__imag__ z1 * __real__ z2);
    printf("prod: %.1f + %.1fi\n", r_prod, i_prod);

    double den = (__real__ z1 * __real__ z1) + (__imag__ z1 * __imag__ z1);
    double r_quot = ((__real__ z2 * __real__ z1) + (__imag__ z2 * __imag__ z1)) / den;
    double i_quot = ((__imag__ z2 * __real__ z1) - (__real__ z2 * __imag__ z1)) / den;
    printf("quot: %.1f + %.1fi\n", r_quot, i_quot);
    return 0;
}
