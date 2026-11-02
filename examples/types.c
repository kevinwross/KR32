// Built-in C scalar types on KR32.
//
// The M4 C compiler supports _Bool, char, and int (including signed/unsigned
// spellings, which currently map to the same underlying C type), plus float and
// double. C has no templates or overloading, so each type gets its own
// Double<Type> function. Values are fixed rather than runtime-random so the
// demo is reproducible. Pointer, array, struct, union, and enum types are
// excluded because they are not arithmetic scalar types.

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

_Bool DoubleBool(_Bool value) {
    return value * value;
}

char DoubleChar(char value) {
    return value * value;
}

signed char DoubleSChar(signed char value) {
    return value * value;
}

unsigned char DoubleUChar(unsigned char value) {
    return value * value;
}

int DoubleInt(int value) {
    return value * value;
}

signed int DoubleSInt(signed int value) {
    return value * value;
}

unsigned int DoubleUInt(unsigned int value) {
    return value * value;
}

float DoubleFloat(float value) {
    return value * value;
}

double DoubleDouble(double value) {
    return value * value;
}

int main(void) {
    _Bool bool_value = 1;
    char char_value = 7;
    signed char signed_char_value = 5;
    unsigned char unsigned_char_value = 6;
    int int_value = 12;
    signed int signed_int_value = 9;
    unsigned int unsigned_int_value = 10;
    float float_value = 2.5f;
    double double_value = 3.25;

    print_string("bool 1 squared = ");
    print_int(DoubleBool(bool_value));
    putchar('\n');

    print_string("char 7 squared = ");
    print_int(DoubleChar(char_value));
    putchar('\n');

    print_string("signed char 5 squared = ");
    print_int(DoubleSChar(signed_char_value));
    putchar('\n');

    print_string("unsigned char 6 squared = ");
    print_int(DoubleUChar(unsigned_char_value));
    putchar('\n');

    print_string("int 12 squared = ");
    print_int(DoubleInt(int_value));
    putchar('\n');

    print_string("signed int 9 squared = ");
    print_int(DoubleSInt(signed_int_value));
    putchar('\n');

    print_string("unsigned int 10 squared = ");
    print_int(DoubleUInt(unsigned_int_value));
    putchar('\n');

    print_string("float 2.5 squared = ");
    print_int((int)DoubleFloat(float_value));
    putchar('\n');

    print_string("double 3.25 squared = ");
    print_int((int)DoubleDouble(double_value));
    putchar('\n');

    return 0;
}
