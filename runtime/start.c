// the entry module's init and start, which codegen exports under these plain C names, since every symbol cherimoya defines has a dot in it and these don't
void cheri_init(void);
long long cheri_start(void);

// the C entry point of a program runs the init, then the program's start, and hands its result back as the exit code
int main(void) {
    cheri_init();
    return (int)cheri_start();
}
