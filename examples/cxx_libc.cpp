#include <stdio.h>
#include <string.h>

int main() {
    char text[16];
    strcpy(text, "shared libc");
    printf("C++ via C libc: %s\n", text);
    return 0;
}
