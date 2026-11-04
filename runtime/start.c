// the entry module's init, start, and fini, which codegen exports under these plain C names, since every symbol cherimoya defines has a dot in it and these don't
void cheri_init(void);
long long cheri_start(void);
void cheri_fini(void);

// the C entry point of a program runs the init, then the program's start, then the fini, which frees what the top-level bindings hold, and hands start's result back as the exit code
int main(void) {
    cheri_init();
    int code = (int)cheri_start();
    cheri_fini();
    return code;
}
