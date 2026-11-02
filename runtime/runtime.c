#include <stdio.h>
#include <stdlib.h>

// reports a failed runtime check, like assert, with the line it's on, and stops the program
_Noreturn void cheri_trap(const char* message, long long line) {
    fprintf(stderr, "trap at line %lld: %s\n", line, message);
    abort();
}

// reports an index past the end of an array, with the index, the array as it's written, and how many it holds, and stops the program
_Noreturn void cheri_trap_index(unsigned long long index, unsigned long long length, const char* array, long long line) {
    fprintf(stderr, "trap at line %lld: index %llu is past the end of '%s', which holds %llu\n", line, index, array, length);
    abort();
}
