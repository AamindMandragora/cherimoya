#include <stdio.h>
#include <stdlib.h>

// the entry module's main, which codegen exports under this name
extern long long cheri_entry(void) __asm__("cheri.main");

extern void cheri_init(void) __asm__("cheri.init");

// reports a failed runtime check, like assert, with the line it's on, and stops the program
_Noreturn void cheri_trap(const char* message, long long line) __asm__("cheri.trap");
void cheri_trap(const char* message, long long line) {
    fprintf(stderr, "trap at line %lld: %s\n", line, message);
    abort();
}

// the C entry point runs the program and hands its result back as the exit code
int main(void) {
    cheri_init();
    return (int)cheri_entry();
}
