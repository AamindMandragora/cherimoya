#include <stdio.h>
#include <stdlib.h>

// the test file's init, and the function codegen makes that runs one of its tests by number, in the order they're written
void cheri_init(void);
void cheri_run_test(long long index);

// the C entry point of a test binary runs the init, then the one test its argument numbers, so cheri test can run each test in a process of its own, where a test passes by returning, and fails by trapping, which aborts
int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <test number>, which 'cheri test' passes\n", argv[0]);
        return 2;
    }
    cheri_init();
    cheri_run_test(atoll(argv[1]));
    return 0;
}
