#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

// allocates count values of size bytes each on the heap, zeroed when asked, and traps with the message when they wouldn't fit in memory at all or malloc fails, where nothing at all needs no memory, so it's null, which free takes
void* cheri_alloc(unsigned long long count, unsigned long long size, int zeroed, const char* message, long long line) {
    if (count == 0 || size == 0) return NULL;
    if (count > (unsigned long long)-1 / size) cheri_trap(message, line);
    void* memory = zeroed ? calloc(count, size) : malloc(count * size);
    if (memory == NULL) cheri_trap(message, line);
    return memory;
}
