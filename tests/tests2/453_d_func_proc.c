#if defined test_func_pure
func int add(int a, int b) {
    return a + b;
}
#elif defined test_proc
proc void do_something() {
    int x = 42;
}
#endif

int main() {
    return 0;
}
