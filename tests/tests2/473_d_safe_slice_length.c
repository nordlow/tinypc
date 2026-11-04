#include <stdio.h>

typedef int[..] Ints;

#if defined test_safe_assign
int main(void) @safe {
    int a[4] = {1, 2, 3, 4};
    Ints s = a;
    s.length = 2;
    return 0;
}

#elif defined test_safe_compound_assign
int main(void) @safe {
    int a[4] = {1, 2, 3, 4};
    Ints s = a;
    s.length += 1;
    return 0;
}

#elif defined test_safe_increment
int main(void) @safe {
    int a[4] = {1, 2, 3, 4};
    Ints s = a;
    ++s.length;
    return 0;
}

#elif defined test_safe_decrement
int main(void) @safe {
    int a[4] = {1, 2, 3, 4};
    Ints s = a;
    s.length--;
    return 0;
}

#elif defined test_safe_block
int main(void) {
    int a[4] = {1, 2, 3, 4};
    Ints s = a;
    @safe {
        s.length = 2;
    }
    return 0;
}

#elif defined test_safe_trusted_block
int main(void) {
    int a[4] = {1, 2, 3, 4};
    Ints s = a;
    @safe {
        @trusted {
            s.length = 2;
        }
    }
    printf("%ld\n", (long)s.length);
    return 0;
}

#elif defined test_safe_system_block
int main(void) {
    int a[4] = {1, 2, 3, 4};
    Ints s = a;
    @safe {
        @system {
            s.length = 3;
        }
    }
    printf("%ld\n", (long)s.length);
    return 0;
}

#elif defined test_safe_read
int get_length(Ints s) @safe {
    return (int)s.length;
}

int main(void) {
    int a[4] = {1, 2, 3, 4};
    printf("%d\n", get_length(a));
    return 0;
}

#elif defined test_system_assign
int main(void) @system {
    int a[4] = {1, 2, 3, 4};
    Ints s = a;
    s.length = 2;
    printf("%ld\n", (long)s.length);
    return 0;
}

#elif defined test_trusted_assign
int main(void) @trusted {
    int a[4] = {1, 2, 3, 4};
    Ints s = a;
    s.length = 2;
    printf("%ld\n", (long)s.length);
    return 0;
}

#elif defined test_inferred_system
void shrink(Ints s) {
    s.length = 0;
}

int main(void) @safe {
    int a[4] = {1, 2, 3, 4};
    shrink(a);
    return 0;
}
#endif
