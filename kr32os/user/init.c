int sys_write(int descriptor, const char *buffer, int length);
int sys_read(int descriptor, char *buffer, int length);
int sys_yield(void);
int sys_getpid(void);
int sys_brk(unsigned int address);

int main(void) {
    const char *message = "init: hello from user mode\n";
    sys_write(1, message, 27);
    if (sys_getpid() == 1) {
        const char *identity = "init: pid 1\n";
        sys_write(1, identity, 12);
    }
    (void)sys_yield();
    (void)sys_read(0, (char *)0, 0);
    (void)sys_brk(0x00400000);
    return 0;
}
