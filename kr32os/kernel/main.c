int putchar(int character);

void kmain(void) {
    const char *message = "KR32OS M1\n";
    while (*message != '\0') {
        putchar(*message);
        ++message;
    }
}
