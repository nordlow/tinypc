#include <stdio.h>

int main(void) {
    x := 42;
    y := 3.14;
    p := &x;
    *p = 100;
    printf("x = %d, y = %.2f\n", x, y);
    return 0;
}
