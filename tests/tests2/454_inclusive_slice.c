#if defined test_inclusive_slice
typedef int IntSlice[..];
int main() {
    int arr[5] = {10, 20, 30, 40, 50};
    IntSlice s = arr[1..=3];
    printf("%ld\n", (long)s.length);
    return 0;
}
#elif defined test_foreach_inclusive
int main() {
    int sum = 0;
    foreach (i; 1..=5) {
        sum += i;
    }
    printf("sum=%d\n", sum);
    return 0;
}
#endif
