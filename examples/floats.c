// Floating-point scalar coverage for the KR32 C compiler.
//
// Demonstrates float and double literals, locals, globals, arithmetic,
// comparisons, casts, FP arguments, and FP return values. The console printer
// is integer-only, so FP results are intentionally cast to int for display.

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

double global_double = 4.5;

float DoubleFloat(float value) {
    return value * value;
}

double DoubleDouble(double value) {
    return value * value;
}

double fpCrazyTown(double Smack, float Crackle, double Pop, float Boom) {
    double Fizzle =  22.0 / 7.0;
    double Bizzle = Smack + Pop + Fizzle;
    double Cizzle = Crackle + Boom + Fizzle;
    return Fizzle * 2.0 + Bizzle * Crackle / 2.0 + Pop - Boom * Cizzle;
}

double MixValues(int integer_part, double double_part, float float_part) {
    return integer_part + double_part + float_part;
}

int main(void) {
    float f = 2.5f;
    double d = 3.25;
    float sum = f + (float)d;
    double mixed = MixValues(3, 2.5, 1.25f);

    print_string("float 2.5 squared = ");
    print_int((int)DoubleFloat(f)); 
    putchar('\n');

    printf("fpCrazyTown result = %f\n", fpCrazyTown(1.0, 2.0f, 3.0, 4.0f));


    print_string("double 3.25 squared = ");
    print_int((int)DoubleDouble(d));
    putchar('\n');

    print_string("2.5 + 3.25 = ");
    print_int((int)sum);
    putchar('\n');

    print_string("3 + 2.5 + 1.25 = ");
    print_int((int)mixed);
    putchar('\n');

    print_string("global 4.5 squared = ");
    print_int((int)DoubleDouble(global_double));
    putchar('\n');

    if (d > f && mixed >= 6.0) {
        print_string("comparisons: ok");
        putchar('\n');
    } else {
        print_string("comparisons: failed");
        putchar('\n');
    }

    return 0;
}
