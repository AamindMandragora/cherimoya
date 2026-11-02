#include <stdio.h>
#include <stdlib.h>

// reports a failed runtime check, like assert, with the line it's on, and stops the program
_Noreturn void cheri_trap(const char* message, long long line) {
    fprintf(stderr, "trap at line %lld: %s\n", line, message);
    abort();
}
