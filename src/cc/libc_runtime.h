#pragma once

namespace kr32::cc::libc_source {

inline constexpr const char* kLibcSource = R"KR32LIBC(
#include <stdarg.h>
#include <stddef.h>

int putchar(int c);
int getchar(void);
void exit(int code);

void *memcpy(void *destination, const void *source, int count);
void *memset(void *destination, int value, int count);

struct heap_block {
    unsigned int size;
    unsigned int is_free;
    unsigned int next;
    unsigned int prev;
};

static struct heap_block *heap_free_list = 0;
static unsigned int heap_begin = 0;
static unsigned int heap_end_address = 0;
static int heap_ready = 0;

static unsigned int heap_align(unsigned int value, unsigned int alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

static unsigned int heap_block_size(unsigned int payload) {
    unsigned int total = payload + sizeof(struct heap_block) + 4;
    unsigned int minimum = sizeof(struct heap_block) + 12;
    total = heap_align(total, 8);
    if (total < minimum) {
        total = minimum;
    }
    return total;
}

static unsigned int heap_footer(struct heap_block *block) {
    unsigned int address = (unsigned int)block + block->size - 4;
    return *(unsigned int *)address;
}

static void heap_set_footer(struct heap_block *block) {
    unsigned int address = (unsigned int)block + block->size - 4;
    *(unsigned int *)address = block->size;
}

static struct heap_block *heap_next(struct heap_block *block) {
    unsigned int address = (unsigned int)block + block->size;
    if (address + sizeof(struct heap_block) > heap_end_address) {
        return 0;
    }
    return (struct heap_block *)address;
}

static void heap_init(void) {
    unsigned int begin = (unsigned int)__builtin_heap_start();
    unsigned int end = (unsigned int)__builtin_heap_end();
    begin = heap_align(begin, 8);
    end = end & ~7;
    heap_begin = begin;
    heap_end_address = end;
    if (begin + sizeof(struct heap_block) + 12 > end) {
        return;
    }
    struct heap_block *block = (struct heap_block *)begin;
    block->size = end - begin;
    block->is_free = 1;
    block->next = 0;
    block->prev = 0;
    heap_set_footer(block);
    heap_free_list = block;
    heap_ready = 1;
}

static void heap_remove(struct heap_block *block) {
    struct heap_block *previous = (struct heap_block *)block->prev;
    struct heap_block *next = (struct heap_block *)block->next;
    if (previous != 0) {
        previous->next = (unsigned int)next;
    } else {
        heap_free_list = next;
    }
    if (next != 0) {
        next->prev = (unsigned int)previous;
    }
    block->next = 0;
    block->prev = 0;
}

static void heap_insert(struct heap_block *block) {
    struct heap_block *previous = 0;
    struct heap_block *current = heap_free_list;
    while (current != 0 && (unsigned int)current < (unsigned int)block) {
        previous = current;
        current = (struct heap_block *)current->next;
    }
    block->prev = (unsigned int)previous;
    block->next = (unsigned int)current;
    if (previous != 0) {
        previous->next = (unsigned int)block;
    } else {
        heap_free_list = block;
    }
    if (current != 0) {
        current->prev = (unsigned int)block;
    }
}

void *malloc(int size) {
    if (size <= 0) {
        return 0;
    }
    if (!heap_ready) {
        heap_init();
    }
    if (!heap_ready) {
        return 0;
    }
    unsigned int needed = heap_block_size((unsigned int)size);
    struct heap_block *best = 0;
    struct heap_block *current = heap_free_list;
    while (current != 0) {
        if (current->is_free && current->size >= needed) {
            if (best == 0 || current->size < best->size) {
                best = current;
                if (current->size == needed) {
                    break;
                }
            }
        }
        current = current->next;
    }
    if (best == 0) {
        return 0;
    }
    heap_remove(best);
    unsigned int minimum = sizeof(struct heap_block) + 12;
    if (best->size >= needed + minimum) {
        struct heap_block *remainder =
            (struct heap_block *)((unsigned int)best + needed);
        remainder->size = best->size - needed;
        remainder->is_free = 1;
        remainder->next = 0;
        remainder->prev = 0;
        heap_set_footer(remainder);
        best->size = needed;
        heap_insert(remainder);
    }
    best->is_free = 0;
    heap_set_footer(best);
    return (void *)((unsigned int)best + sizeof(struct heap_block));
}

void free(void *pointer) {
    if (pointer == 0) {
        return;
    }
    struct heap_block *block =
        (struct heap_block *)((unsigned int)pointer - sizeof(struct heap_block));
    if (block->is_free) {
        return;
    }
    block->is_free = 1;

    struct heap_block *next = heap_next(block);
    if (next != 0 && next->is_free) {
        heap_remove(next);
        block->size = block->size + next->size;
        heap_set_footer(block);
    }

    if ((unsigned int)block > heap_begin) {
        unsigned int previous_size = *(unsigned int *)((unsigned int)block - 4);
        struct heap_block *previous =
            (struct heap_block *)((unsigned int)block - previous_size);
        if (previous->is_free) {
            heap_remove(previous);
            previous->size = previous->size + block->size;
            heap_set_footer(previous);
            block = previous;
        }
    }
    heap_insert(block);
}

void *calloc(int count, int size) {
    if (count <= 0 || size <= 0) {
        return 0;
    }
    unsigned int total = (unsigned int)count * (unsigned int)size;
    if (count != 0 && total / (unsigned int)count != (unsigned int)size) {
        return 0;
    }
    void *pointer = malloc((int)total);
    if (pointer != 0) {
        memset(pointer, 0, (int)total);
    }
    return pointer;
}

void *realloc(void *pointer, int size) {
    if (pointer == 0) {
        return malloc(size);
    }
    if (size <= 0) {
        free(pointer);
        return 0;
    }
    struct heap_block *block =
        (struct heap_block *)((unsigned int)pointer - sizeof(struct heap_block));
    unsigned int old_size = block->size - sizeof(struct heap_block) - 4;
    if (old_size >= (unsigned int)size) {
        return pointer;
    }
    void *replacement = malloc(size);
    if (replacement == 0) {
        return 0;
    }
    memcpy(replacement, pointer, (int)old_size);
    free(pointer);
    return replacement;
}

static int out_char(char c) {
    putchar((int)c);
    return 1;
}

static int out_pad(int count, char c) {
    int written = 0;
    while (written < count) {
        putchar((int)c);
        written = written + 1;
    }
    return written;
}

static int out_field(const char *text, int length, int width, int flags) {
    int left = flags & 1;
    int zero = (flags >> 1) & 1;
    int prefix = flags >> 8;
    int padding = width - length;
    int written = 0;
    int index = 0;
    if (padding < 0) {
        padding = 0;
    }
    if (left) {
        while (index < length) {
            putchar((int)text[index]);
            written = written + 1;
            index = index + 1;
        }
        written = written + out_pad(padding, ' ');
        return written;
    }
    if (zero) {
        while (index < prefix && index < length) {
            putchar((int)text[index]);
            written = written + 1;
            index = index + 1;
        }
        written = written + out_pad(padding, '0');
        while (index < length) {
            putchar((int)text[index]);
            written = written + 1;
            index = index + 1;
        }
        return written;
    }
    written = written + out_pad(padding, ' ');
    while (index < length) {
        putchar((int)text[index]);
        written = written + 1;
        index = index + 1;
    }
    return written;
}

static int format_integer(char *buffer, unsigned int value, int base, int flags) {
    int negative = flags & 1;
    int plus = (flags >> 1) & 1;
    int space = (flags >> 2) & 1;
    int alternate = (flags >> 3) & 1;
    int upper = (flags >> 4) & 1;
    char digits[32];
    int digit_count = 0;
    int position = 0;
    if (negative) {
        buffer[position] = '-';
        position = position + 1;
    } else if (plus) {
        buffer[position] = '+';
        position = position + 1;
    } else if (space) {
        buffer[position] = ' ';
        position = position + 1;
    }
    if (alternate) {
        if (base == 16) {
            buffer[position] = '0';
            position = position + 1;
            if (upper) {
                buffer[position] = 'X';
            } else {
                buffer[position] = 'x';
            }
            position = position + 1;
        } else if (base == 8 && value != 0) {
            buffer[position] = '0';
            position = position + 1;
        }
    }
    if (value == 0) {
        digits[digit_count] = '0';
        digit_count = digit_count + 1;
    }
    while (value != 0) {
        int digit = (int)(value % (unsigned int)base);
        if (digit < 0) {
            digit = 0 - digit;
        }
        if (digit < 10) {
            digits[digit_count] = (char)('0' + digit);
        } else {
            if (upper) {
                digits[digit_count] = (char)('A' + (digit - 10));
            } else {
                digits[digit_count] = (char)('a' + (digit - 10));
            }
        }
        digit_count = digit_count + 1;
        value = value / (unsigned int)base;
    }
    while (digit_count > 0) {
        digit_count = digit_count - 1;
        buffer[position] = digits[digit_count];
        position = position + 1;
    }
    return position;
}

static int format_double_fixed(char *buffer, double value, int precision, int flags) {
    int plus = flags & 1;
    int space = (flags >> 1) & 1;
    int alternate = (flags >> 2) & 1;
    int position = 0;
    if (precision < 0) {
        precision = 6;
    }
    if (precision > 9) {
        precision = 9;
    }
    if (value < 0.0) {
        buffer[position] = '-';
        position = position + 1;
        value = 0.0 - value;
    } else if (plus) {
        buffer[position] = '+';
        position = position + 1;
    } else if (space) {
        buffer[position] = ' ';
        position = position + 1;
    }
    double scale = 1.0;
    int index = 0;
    while (index < precision) {
        scale = scale * 10.0;
        index = index + 1;
    }
    double scaled = value * scale + 0.5;
    unsigned int whole = (unsigned int)(scaled / scale);
    unsigned int fraction = (unsigned int)(scaled - (double)whole * scale);
    if ((double)fraction >= scale) {
        whole = whole + 1;
        fraction = 0;
    }
    char digits[40];
    int digit_count = 0;
    if (whole == 0) {
        digits[digit_count] = '0';
        digit_count = digit_count + 1;
    }
    while (whole > 0) {
        digits[digit_count] = (char)('0' + (int)(whole % 10));
        digit_count = digit_count + 1;
        whole = whole / 10;
    }
    while (digit_count > 0) {
        digit_count = digit_count - 1;
        buffer[position] = digits[digit_count];
        position = position + 1;
    }
    if (precision > 0 || alternate) {
        buffer[position] = '.';
        position = position + 1;
    }
    if (precision > 0) {
        unsigned int divisor = 1;
        index = 1;
        while (index < precision) {
            divisor = divisor * 10;
            index = index + 1;
        }
        while (divisor > 0) {
            buffer[position] = (char)('0' + (int)((fraction / divisor) % 10));
            position = position + 1;
            divisor = divisor / 10;
        }
    }
    return position;
}

static int format_double_exp(char *buffer, double value, int precision, int flags) {
    int plus = flags & 1;
    int space = (flags >> 1) & 1;
    int alternate = (flags >> 2) & 1;
    int upper = (flags >> 3) & 1;
    int position = 0;
    if (precision < 0) {
        precision = 6;
    }
    if (precision > 9) {
        precision = 9;
    }
    if (value < 0.0) {
        buffer[position] = '-';
        position = position + 1;
        value = 0.0 - value;
    } else if (plus) {
        buffer[position] = '+';
        position = position + 1;
    } else if (space) {
        buffer[position] = ' ';
        position = position + 1;
    }
    int exponent = 0;
    if (value != 0.0) {
        while (value >= 10.0) {
            value = value / 10.0;
            exponent = exponent + 1;
        }
        while (value < 1.0) {
            value = value * 10.0;
            exponent = exponent - 1;
        }
    }
    double scale = 1.0;
    int index = 0;
    while (index < precision) {
        scale = scale * 10.0;
        index = index + 1;
    }
    double scaled = value * scale + 0.5;
    if (scaled >= scale * 10.0) {
        value = value / 10.0;
        exponent = exponent + 1;
        scaled = value * scale + 0.5;
    }
    unsigned int whole = (unsigned int)(scaled / scale);
    unsigned int fraction = (unsigned int)(scaled - (double)whole * scale);
    if (whole > 9) {
        whole = 9;
    }
    buffer[position] = (char)('0' + (int)whole);
    position = position + 1;
    if (precision > 0 || alternate) {
        buffer[position] = '.';
        position = position + 1;
    }
    if (precision > 0) {
        unsigned int divisor = 1;
        index = 1;
        while (index < precision) {
            divisor = divisor * 10;
            index = index + 1;
        }
        while (divisor > 0) {
            buffer[position] = (char)('0' + (int)((fraction / divisor) % 10));
            position = position + 1;
            divisor = divisor / 10;
        }
    }
    if (upper) {
        buffer[position] = 'E';
    } else {
        buffer[position] = 'e';
    }
    position = position + 1;
    if (exponent < 0) {
        buffer[position] = '-';
        position = position + 1;
        exponent = 0 - exponent;
    } else {
        buffer[position] = '+';
        position = position + 1;
    }
    char exp_digits[8];
    int exp_count = 0;
    if (exponent == 0) {
        exp_digits[exp_count] = '0';
        exp_count = exp_count + 1;
    }
    while (exponent > 0) {
        exp_digits[exp_count] = (char)('0' + (exponent % 10));
        exp_count = exp_count + 1;
        exponent = exponent / 10;
    }
    if (exp_count < 2) {
        buffer[position] = '0';
        position = position + 1;
    }
    while (exp_count > 0) {
        exp_count = exp_count - 1;
        buffer[position] = exp_digits[exp_count];
        position = position + 1;
    }
    return position;
}

static int decimal_exponent(double value) {
    int exponent = 0;
    if (value < 0.0) {
        value = 0.0 - value;
    }
    if (value == 0.0) {
        return 0;
    }
    while (value >= 10.0) {
        value = value / 10.0;
        exponent = exponent + 1;
    }
    while (value < 1.0) {
        value = value * 10.0;
        exponent = exponent - 1;
    }
    return exponent;
}

static int strip_float(char *buffer, int length, int alternate) {
    if (alternate) {
        return length;
    }
    int exponent_position = -1;
    int dot_position = -1;
    int index = 0;
    while (index < length) {
        if (buffer[index] == 'e' || buffer[index] == 'E') {
            exponent_position = index;
            break;
        }
        if (buffer[index] == '.') {
            dot_position = index;
        }
        index = index + 1;
    }
    if (dot_position < 0) {
        return length;
    }
    int end = length;
    if (exponent_position >= 0) {
        end = exponent_position;
    }
    while (end > dot_position + 1 && buffer[end - 1] == '0') {
        end = end - 1;
    }
    if (end == dot_position + 1) {
        end = dot_position;
    }
    if (exponent_position >= 0) {
        int tail = exponent_position;
        while (tail < length) {
            buffer[end] = buffer[tail];
            end = end + 1;
            tail = tail + 1;
        }
        length = end;
    } else {
        length = end;
    }
    return length;
}

static int format_double_general(char *buffer, double value, int precision, int flags) {
    int alternate = (flags >> 2) & 1;
    if (precision < 0) {
        precision = 6;
    }
    if (precision == 0) {
        precision = 1;
    }
    int exponent = decimal_exponent(value);
    int length = 0;
    if (exponent < -4 || exponent >= precision) {
        length = format_double_exp(buffer, value, precision - 1, flags);
    } else {
        int fraction = precision - exponent - 1;
        if (fraction < 0) {
            fraction = 0;
        }
        length = format_double_fixed(buffer, value, fraction, flags);
    }
    return strip_float(buffer, length, alternate);
}

double sqrt(double value) {
    return __builtin_sqrt(value);
}

float sqrtf(float value) {
    return __builtin_sqrt(value);
}

double fabs(double value) {
    return __builtin_fabs(value);
}

float fabsf(float value) {
    return __builtin_fabs(value);
}

double fmin(double left, double right) {
    return __builtin_fmin(left, right);
}

float fminf(float left, float right) {
    return __builtin_fmin(left, right);
}

double fmax(double left, double right) {
    return __builtin_fmax(left, right);
}

float fmaxf(float left, float right) {
    return __builtin_fmax(left, right);
}

int printf(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    int total = 0;
    const char *cursor = format;
    char buffer[160];
    while (*cursor != '\0') {
        if (*cursor != '%') {
            putchar((int)*cursor);
            total = total + 1;
            cursor = cursor + 1;
            continue;
        }
        cursor = cursor + 1;
        if (*cursor == '%') {
            putchar('%');
            total = total + 1;
            cursor = cursor + 1;
            continue;
        }
        int left = 0;
        int plus = 0;
        int space = 0;
        int alternate = 0;
        int zero = 0;
        while (*cursor == '-' || *cursor == '+' || *cursor == ' ' ||
               *cursor == '#' || *cursor == '0') {
            if (*cursor == '-') left = 1;
            if (*cursor == '+') plus = 1;
            if (*cursor == ' ') space = 1;
            if (*cursor == '#') alternate = 1;
            if (*cursor == '0') zero = 1;
            cursor = cursor + 1;
        }
        int width = 0;
        if (*cursor == '*') {
            width = va_arg(arguments, int);
            cursor = cursor + 1;
            if (width < 0) {
                left = 1;
                width = 0 - width;
            }
        } else {
            while (*cursor >= '0' && *cursor <= '9') {
                width = width * 10 + (*cursor - '0');
                cursor = cursor + 1;
            }
        }
        int precision = -1;
        if (*cursor == '.') {
            cursor = cursor + 1;
            precision = 0;
            if (*cursor == '*') {
                precision = va_arg(arguments, int);
                cursor = cursor + 1;
                if (precision < 0) {
                    precision = -1;
                }
            } else {
                while (*cursor >= '0' && *cursor <= '9') {
                    precision = precision * 10 + (*cursor - '0');
                    cursor = cursor + 1;
                }
            }
        }
        while (*cursor == 'h' || *cursor == 'l' || *cursor == 'j' ||
               *cursor == 'z' || *cursor == 't' || *cursor == 'L') {
            cursor = cursor + 1;
        }
        char spec = *cursor;
        if (spec != '\0') {
            cursor = cursor + 1;
        }
        int length = 0;
        int prefix = 0;
        if (spec == 'd' || spec == 'i') {
            int value = va_arg(arguments, int);
            unsigned int magnitude = 0;
            int negative = value < 0;
            if (negative) {
                magnitude = (unsigned int)(0 - value);
            } else {
                magnitude = (unsigned int)value;
            }
            int integer_flags = 0;
            if (negative) integer_flags = integer_flags | 1;
            if (plus) integer_flags = integer_flags | 2;
            if (space) integer_flags = integer_flags | 4;
            length = format_integer(buffer, magnitude, 10, integer_flags);
            if (negative || plus || space) {
                prefix = 1;
            }
        } else if (spec == 'u' || spec == 'x' || spec == 'X' || spec == 'o') {
            unsigned int value = va_arg(arguments, unsigned int);
            int base = 10;
            int integer_flags = 0;
            if (spec == 'x' || spec == 'X') {
                base = 16;
                if (alternate) integer_flags = integer_flags | 8;
                if (spec == 'X') integer_flags = integer_flags | 16;
            } else if (spec == 'o') {
                base = 8;
                if (alternate) integer_flags = integer_flags | 8;
            }
            length = format_integer(buffer, value, base, integer_flags);
            if ((spec == 'x' || spec == 'X') && alternate && value != 0) {
                prefix = 2;
            } else if (spec == 'o' && alternate && value != 0) {
                prefix = 1;
            }
        } else if (spec == 'p') {
            unsigned int value = (unsigned int)va_arg(arguments, void *);
            length = format_integer(buffer, value, 16, 8);
            prefix = 2;
        } else if (spec == 'c') {
            buffer[0] = (char)va_arg(arguments, int);
            length = 1;
        } else if (spec == 's') {
            const char *text = va_arg(arguments, const char *);
            if (text == 0) {
                text = "(null)";
            }
            length = 0;
            while (text[length] != '\0' && (precision < 0 || length < precision)) {
                length = length + 1;
            }
            int field_flags = 0;
            if (left) {
                field_flags = 1;
            }
            total = total + out_field(text, length, width, field_flags);
            continue;
        } else if (spec == 'f' || spec == 'F') {
            double value = va_arg(arguments, double);
            int float_flags = 0;
            if (plus) float_flags = float_flags | 1;
            if (space) float_flags = float_flags | 2;
            if (alternate) float_flags = float_flags | 4;
            int fp_precision = precision;
            if (fp_precision < 0) {
                fp_precision = 6;
            }
            length = format_double_fixed(buffer, value, fp_precision, float_flags);
            if (value < 0.0 || plus || space) {
                prefix = 1;
            }
        } else if (spec == 'e' || spec == 'E') {
            double value = va_arg(arguments, double);
            int float_flags = 0;
            if (plus) float_flags = float_flags | 1;
            if (space) float_flags = float_flags | 2;
            if (alternate) float_flags = float_flags | 4;
            if (spec == 'E') float_flags = float_flags | 8;
            int fp_precision = precision;
            if (fp_precision < 0) {
                fp_precision = 6;
            }
            length = format_double_exp(buffer, value, fp_precision, float_flags);
            if (value < 0.0 || plus || space) {
                prefix = 1;
            }
        } else if (spec == 'g' || spec == 'G') {
            double value = va_arg(arguments, double);
            int float_flags = 0;
            if (plus) float_flags = float_flags | 1;
            if (space) float_flags = float_flags | 2;
            if (alternate) float_flags = float_flags | 4;
            if (spec == 'G') float_flags = float_flags | 8;
            int fp_precision = precision;
            if (fp_precision < 0) {
                fp_precision = 6;
            }
            length = format_double_general(buffer, value, fp_precision, float_flags);
            if (value < 0.0 || plus || space) {
                prefix = 1;
            }
        } else {
            buffer[0] = '%';
            buffer[1] = spec;
            length = 2;
        }
        int field_flags = 0;
        if (left) field_flags = field_flags | 1;
        if (zero) field_flags = field_flags | 2;
        field_flags = field_flags | (prefix << 8);
        total = total + out_field(buffer, length, width, field_flags);
    }
    va_end(arguments);
    return total;
}
)KR32LIBC";

} // namespace kr32::cc::libc_source
