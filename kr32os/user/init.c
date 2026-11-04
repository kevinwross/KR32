int sys_write(int descriptor, const char *buffer, int length);

int main(void) {
    const char *message = "init: hello from user mode\n";
    sys_write(1, message, 27);
    return 0;
}
