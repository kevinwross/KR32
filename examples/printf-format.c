// printf format coverage for the KR32 C library.
//
// Each check formats a value with snprintf and compares the text against the
// expected result, so a formatting defect fails the test run rather than
// showing up as a wrong number in someone's output.

int printf(const char *format, ...);
int snprintf(char *buffer, int size, const char *format, ...);
int strcmp(const char *left, const char *right);
int strlen(const char *text);

static int failures = 0;
static int checks = 0;

void expect(const char *expected, const char *actual) {
    checks = checks + 1;
    if (strcmp(expected, actual) != 0) {
        failures = failures + 1;
        printf("FAIL: expected [%s] got [%s]\n", expected, actual);
    }
}

// A double parameter, so the call inside uses the variadic convention.
void expect_double(const char *expected, const char *format, double value) {
    char buffer[64];
    snprintf(buffer, 64, format, value);
    expect(expected, buffer);
}

void expect_int(const char *expected, const char *format, int value) {
    char buffer[64];
    snprintf(buffer, 64, format, value);
    expect(expected, buffer);
}

// A format with no conversions, so nothing is substituted.
void expect_literal(const char *expected, const char *format) {
    char buffer[64];
    snprintf(buffer, 64, format);
    expect(expected, buffer);
}

void expect_text(const char *expected, const char *format, const char *value) {
    char buffer[64];
    snprintf(buffer, 64, format, value);
    expect(expected, buffer);
}

int main(void) {
    // Fixed point, default and explicit precision.
    expect_double("2.500000", "%f", 2.5);
    expect_double("0.500000", "%f", 0.5);
    expect_double("-2.500000", "%f", -2.5);
    expect_double("3.142", "%.3f", 3.14159265);
    expect_double("0.333", "%.3f", 1.0 / 3.0);
    expect_double("2.0", "%.1f", 2.0);

    // Ties round to even, as C requires and IEEE-754 specifies.
    expect_double("2", "%.0f", 2.5);
    expect_double("4", "%.0f", 3.5);
    expect_double("2", "%.0f", 1.5);
    expect_double("0", "%.0f", 0.5);
    expect_double("-2", "%.0f", -2.5);
    expect_double("2.2", "%.1f", 2.25);

    // Sign, space, and zero-padding interact with the field width.
    expect_double(" 2.000000", "% f", 2.0);
    expect_double("-2.000000", "% f", -2.0);
    expect_double("+2.000000", "%+f", 2.0);
    expect_double("-0003.50", "%08.2f", -3.5);
    expect_double("00003.50", "%08.2f", 3.5);
    expect_double("   3.142", "%8.3f", 3.14159265);
    expect_double("3.1     |", "%-8.1f|", 3.14);

    // Exponent and general forms.
    expect_double("3.141590e+02", "%e", 314.159);
    expect_double("1.230000E-04", "%E", 0.000123);
    expect_double("1.5e+03", "%.1e", 1500.0);
    expect_double("3.14159", "%g", 3.14159265);
    expect_double("1e+06", "%g", 1000000.0);
    expect_double(" 2", "% g", 2.0);
    expect_double("+2", "%+g", 2.0);

    // A value that exercises decimal carry out of the fraction.
    expect_double("10.00", "%.2f", 9.999);
    expect_double("1000.000000", "%f", 1000.0);

    // Integer conversions, widths, and left justification.
    expect_int("-42", "%d", -42);
    expect_int("   42", "%5d", 42);
    expect_int("42   |", "%-5d|", 42);
    expect_int("00042", "%05d", 42);
    expect_int("2a", "%x", 42);
    expect_int("2A", "%X", 42);
    expect_int("52", "%o", 42);
    expect_int("42", "%u", 42);

    // Text and character conversions.
    expect_text("hello", "%s", "hello");
    expect_text("   hi", "%5s", "hi");
    expect_text("hi   |", "%-5s|", "hi");

    // A literal percent and a plain string pass through.
    expect_literal("%", "%%");
    expect_literal("no conversions", "no conversions");

    // The return value counts the characters written.
    checks = checks + 1;
    if (snprintf(0, 0, "%d", 12345) != 5) {
        failures = failures + 1;
        printf("FAIL: snprintf return value\n");
    }

    printf("printf format: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
