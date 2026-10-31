#include <stdio.h>
#include <assert.h>

int main() {
    int arr[3] = { 10, 20, 30 };

    // 1. const index and const value (foreach & foreach_reverse)
    {
        int sum = 0;
        foreach (const int i, const int v; arr) {
            assert(i >= 0 && i < 3);
            assert(v == arr[i]);
            sum += v;
        }
        assert(sum == 60);

        int rsum = 0;
        int expected_idx = 2;
        foreach_reverse (const int i, const int v; arr) {
            assert(i == expected_idx);
            assert(v == arr[i]);
            rsum += v;
            expected_idx--;
        }
        assert(rsum == 60);
    }

    // 2. immutable index and immutable value (foreach & foreach_reverse)
    {
        int sum = 0;
        foreach (immutable int i, immutable int v; arr) {
            assert(i >= 0 && i < 3);
            assert(v == arr[i]);
            sum += v;
        }
        assert(sum == 60);

        int rsum = 0;
        int expected_idx = 2;
        foreach_reverse (immutable int i, immutable int v; arr) {
            assert(i == expected_idx);
            assert(v == arr[i]);
            rsum += v;
            expected_idx--;
        }
        assert(rsum == 60);
    }

    // 3. volatile index and volatile value (foreach & foreach_reverse)
    {
        int sum = 0;
        foreach (volatile int i, volatile int v; arr) {
            assert(i >= 0 && i < 3);
            assert(v == arr[i]);
            sum += v;
        }
        assert(sum == 60);

        int rsum = 0;
        int expected_idx = 2;
        foreach_reverse (volatile int i, volatile int v; arr) {
            assert(i == expected_idx);
            assert(v == arr[i]);
            rsum += v;
            expected_idx--;
        }
        assert(rsum == 60);
    }

    // 4. @scoped index and @scoped value (foreach & foreach_reverse)
    {
        int sum = 0;
        foreach (@scoped int i, @scoped int v; arr) {
            assert(i >= 0 && i < 3);
            assert(v == arr[i]);
            sum += v;
        }
        assert(sum == 60);

        int rsum = 0;
        int expected_idx = 2;
        foreach_reverse (@scoped int i, @scoped int v; arr) {
            assert(i == expected_idx);
            assert(v == arr[i]);
            rsum += v;
            expected_idx--;
        }
        assert(rsum == 60);
    }

    // 5. @nocopy index and @nocopy value (foreach & foreach_reverse)
    {
        int sum = 0;
        foreach (@nocopy int i, @nocopy int v; arr) {
            assert(i >= 0 && i < 3);
            assert(v == arr[i]);
            sum += v;
        }
        assert(sum == 60);

        int rsum = 0;
        int expected_idx = 2;
        foreach_reverse (@nocopy int i, @nocopy int v; arr) {
            assert(i == expected_idx);
            assert(v == arr[i]);
            rsum += v;
            expected_idx--;
        }
        assert(rsum == 60);
    }

    printf("ALL FOREACH QUALIFIER TESTS PASSED\n");
    return 0;
}
