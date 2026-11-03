#include <stdio.h>

int main() {
    cfloat cf;
    cdouble cd;
    creal cr;
    ifloat ifl;
    idouble idl;
    ireal irl;

    if (sizeof(cf) > 0 && sizeof(cd) > 0 && sizeof(cr) > 0 &&
        sizeof(ifl) > 0 && sizeof(idl) > 0 && sizeof(irl) > 0) {
        printf("D complex declarations passed\n");
    }
    return 0;
}
