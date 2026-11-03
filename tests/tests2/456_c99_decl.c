#include <stdio.h>

int main() {
    _Complex float cf;
    _Complex double cd;
    _Complex long double cl;
    _Imaginary double id;

    if (sizeof(cf) > 0 && sizeof(cd) > 0 && sizeof(cl) > 0 && sizeof(id) > 0) {
        printf("C99 complex declarations passed\n");
    }
    return 0;
}
