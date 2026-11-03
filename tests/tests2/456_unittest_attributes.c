#include <assert.h>

int square(int x) @pure {
    return x * x;
}

unittest @trusted {
    assert(square(4) == 16);
}

unittest @pure {
    assert(square(5) == 25);
}

int main(void) {
    return 0;
}
