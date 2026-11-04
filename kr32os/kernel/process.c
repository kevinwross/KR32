#include "process.h"
#include "console.h"

struct Process {
    int used;
    int pid;
    int state; // 0 free, 1 runnable, 2 zombie, 3 reaped
    int status;
    char name[32];
};

static struct Process table[256];
static int next_pid = 1;

void process_init(void) {
    int index = 0;
    while (index < 256) {
        table[index].used = 0;
        table[index].pid = 0;
        table[index].state = 0;
        table[index].status = 0;
        table[index].name[0] = '\0';
        ++index;
    }
    next_pid = 1;
}

int process_spawn(const char *name) {
    int index = 0;
    while (index < 256 && table[index].used != 0) {
        ++index;
    }
    if (index >= 256) {
        return -11; // EAGAIN
    }
    table[index].used = 1;
    table[index].pid = next_pid++;
    table[index].state = 1;
    table[index].status = 0;
    int offset = 0;
    while (name[offset] != '\0' && offset < 31) {
        table[index].name[offset] = name[offset];
        ++offset;
    }
    table[index].name[offset] = '\0';
    return table[index].pid;
}

int process_exit(int pid, int status) {
    int index = 0;
    while (index < 256) {
        if (table[index].used != 0 && table[index].pid == pid) {
            table[index].state = 2;
            table[index].status = status;
            return 0;
        }
        ++index;
    }
    return -3; // ESRCH
}

void process_list(void) {
    console_write("PID STATE NAME\n");
    int index = 0;
    while (index < 256) {
        if (table[index].used != 0 && table[index].state != 3) {
            console_write_uint((unsigned int)table[index].pid);
            console_write(" ");
            if (table[index].state == 2) {
                console_write("zombie ");
            } else {
                console_write("running ");
            }
            console_write(table[index].name);
            console_putc('\n');
        }
        ++index;
    }
}
