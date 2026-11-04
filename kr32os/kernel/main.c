int putchar(int character);

void kmain(void) {
    const char *message = "KR32OS M0\n";
    while (*message != '\0') {
        putchar(*message);
        ++message;
    }
}
