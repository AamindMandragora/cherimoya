#include <stdio.h>
#include <stdlib.h>

// the entry module's init and main, which codegen exports under these plain C names, since every symbol cherimoya defines has a dot in it and these don't
void cheri_init(void);
long long cheri_main(void);

// reports a failed runtime check, like assert, with the line it's on, and stops the program
_Noreturn void cheri_trap(const char* message, long long line) {
    fprintf(stderr, "trap at line %lld: %s\n", line, message);
    abort();
}

// the C entry point runs the init, then the program, and hands its result back as the exit code
int main(void) {
    cheri_init();
    return (int)cheri_main();
}
