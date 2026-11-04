#include "console.h"
#include "timer.h"

void kernel_halt(int status);
void kernel_break(void);

static int string_equal(const char *left, const char *right) {
    while (*left != '\0' && *right != '\0') {
        if (*left != *right) return 0;
        ++left;
        ++right;
    }
    return *left == '\0' && *right == '\0';
}

static int starts_with(const char *text, const char *prefix) {
    while (*prefix != '\0') {
        if (*text != *prefix) return 0;
        ++text;
        ++prefix;
    }
    return 1;
}

static void read_line(char *buffer, int capacity) {
    int length = 0;
    for (;;) {
        int character = console_getc();
        if (character < 0) {
            continue;
        }
        if (character == '\n') {
            console_putc('\n');
            buffer[length] = '\0';
            return;
        }
        if (character == 0x7F || character == '\b') {
            if (length > 0) {
                --length;
                console_putc('\b');
                console_putc(' ');
                console_putc('\b');
            }
            continue;
        }
        if (character >= 32 && character <= 126 && length + 1 < capacity) {
            buffer[length] = (char)character;
            ++length;
            console_putc((char)character);
        }
    }
}

static void run_command(const char *line) {
    if (line[0] == '\0') {
        return;
    }
    if (string_equal(line, "help")) {
        console_write("help echo clear mem ps exit\n");
    } else if (starts_with(line, "echo")) {
        const char *argument = line + 4;
        while (*argument == ' ') ++argument;
        console_write(argument);
        console_putc('\n');
    } else if (string_equal(line, "clear")) {
        console_clear();
    } else if (string_equal(line, "mem")) {
        console_write("ram=128MiB pages=32768 used=0 heap=0 ticks=");
        console_write_uint(timer_ticks());
        console_putc('\n');
    } else if (string_equal(line, "ps")) {
        console_write("PID STATE NAME\n");
    } else if (string_equal(line, "crash")) {
        kernel_break();
    } else if (string_equal(line, "exit")) {
        kernel_halt(0);
    } else {
        console_write("unknown command: ");
        console_write(line);
        console_putc('\n');
    }
}

void shell_run(void) {
    char line[128];
    for (;;) {
        console_write("> ");
        read_line(line, (int)sizeof(line));
        run_command(line);
    }
}
