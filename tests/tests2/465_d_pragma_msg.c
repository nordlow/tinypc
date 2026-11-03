#include <stdio.h>

pragma(msg, "compile-time message");
pragma(msg, 42);
$msg("alias message");
pragma(msg, "multi", "part");
$msg(1, 2, 3);
$msg("types: ", int, " ", long);

int main(void) {
    pragma(msg, "block-scope message");
    $msg("block-scope alias");
    printf("runtime\n");
    return 0;
}
