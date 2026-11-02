int putchar(int c);
void exit(int code);

int main(void) {
    char *message = "Hello, KR32!\n";
    while (*message) {
        putchar(*message);
        message = message + 1;
    }
    return 0;
}
