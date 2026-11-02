// Variadic functions with integer and floating-point arguments.
//
// KR32 varargs use uniform 8-byte, 8-byte-aligned stack slots. Integer
// arguments travel in those slots as 32-bit values; float arguments promote to
// double. va_arg advances by one 8-byte slot per value.

#include <stdarg.h>

int putchar(int c);

void print_string(const char *text) {
    while (*text) {
        putchar(*text);
        text = text + 1;
    }
}

void print_int(int value) {
    if (value == 0) {
        putchar('0');
        return;
    }
    if (value < 0) {
        putchar('-');
        value = -value;
    }
    char digits[12];
    int count = 0;
    while (value > 0) {
        digits[count] = '0' + value % 10;
        count = count + 1;
        value = value / 10;
    }
    while (count > 0) {
        count = count - 1;
        putchar(digits[count]);
    }
}

int SumInts(int count, ...) {
    va_list arguments;
    va_start(arguments, count);
    int total = 0;
    for (int index = 0; index < count; index = index + 1) {
        total = total + va_arg(arguments, int);
    }
    va_end(arguments);
    return total;
}

double SumDoubles(int count, ...) {
    va_list arguments;
    va_start(arguments, count);
    double total = 0.0;
    for (int index = 0; index < count; index = index + 1) {
        total = total + va_arg(arguments, double);
    }
    va_end(arguments);
    return total;
}

double MixVariadic(int count, ...) {
    va_list arguments;
    va_start(arguments, count);
    int integer_part = va_arg(arguments, int);
    double double_part = va_arg(arguments, double);
    double float_part = va_arg(arguments, double);
    va_end(arguments);
    return integer_part + double_part + float_part;
}

int main(void) {
    print_string("int varargs 1 + 2 + 3 = ");
    print_int(SumInts(3, 1, 2, 3));
    putchar('\n');

    print_string("double varargs 1.5 + 2.5 = ");
    print_int((int)SumDoubles(2, 1.5, 2.5));
    putchar('\n');

    print_string("mixed varargs 4 + 2.5 + 1.25 = ");
    print_int((int)MixVariadic(3, 4, 2.5, 1.25f));
    putchar('\n');

    return 0;
}
