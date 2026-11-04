int main(void) {
    volatile unsigned int *code = (volatile unsigned int *)0x00400000;
    *code = 1;
    return 0;
}
