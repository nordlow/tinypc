/* Type traits: $is, $isA, $hasIndirections, $hasAliasing. */

#include <stdio.h>

struct Point { int x; int y; };
struct RefPoint { int *x; char *y; };
union Data { int i; float f; };
enum State { Active, Inactive };

int main(void)
{
    printf("struct: %d\n", $is(struct Point == struct));
    printf("union: %d\n", $is(union Data == union));
    printf("enum: %d\n", $is(enum State == enum));
    printf("pointer: %d\n", $is(int* == pointer));
    printf("array: %d\n", $is(int[10] == array));
    printf("int: %d\n", $is(int == int));
    printf("float: %d\n", $is(double == float));

    printf("isA same: %d\n", $isA(int, int));
    printf("isA diff: %d\n", $isA(int, double));

    printf("hasIndirections Point: %d\n", $hasIndirections(struct Point));
    printf("hasIndirections RefPoint: %d\n", $hasIndirections(struct RefPoint));
    printf("hasIndirections int*: %d\n", $hasIndirections(int*));
    printf("hasIndirections int: %d\n", $hasIndirections(int));

    printf("hasAliasing RefPoint: %d\n", $hasAliasing(struct RefPoint));
    printf("hasAliasing int: %d\n", $hasAliasing(int));
    printf("hasIndirections immutable(int)*: %d\n", $hasIndirections(immutable(int)*));
    printf("hasAliasing immutable(int)*: %d\n", $hasAliasing(immutable(int)*));

    printf("isSigned int: %d\n", $isSigned(int));
    printf("isSigned unsigned int: %d\n", $isSigned(unsigned int));
    printf("isUnsigned int: %d\n", $isUnsigned(int));
    printf("isUnsigned unsigned int: %d\n", $isUnsigned(unsigned int));

    printf("isConst int: %d\n", $isConst(int));
    printf("isConst const int: %d\n", $isConst(const int));
    printf("isVolatile volatile int: %d\n", $isVolatile(volatile int));
    printf("isImmutable immutable(int): %d\n", $isImmutable(immutable(int)));

    return 0;
}
