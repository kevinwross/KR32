int open(const char *path, int flags);
int close(int fd);
int write(int fd, const void *buffer, int count);

int main(void) {
    if (open("../escape.txt", 1 | 4) >= 0) {
        return 3;
    }
    int descriptor = open("host-output.txt", 1 | 4 | 8);
    if (descriptor < 0) {
        return 2;
    }
    const char *message = "hello from KR32\n";
    write(descriptor, message, 16);
    close(descriptor);
    return 0;
}
