/* Reads one line from the console and echoes it, so the read syscall has a task
   the serial-driven tests can exercise. */

#include <kr32/con.h>
#include <kr32/sys.h>

int main(void) {
    char line[64];
    const int count = sys_read(0, line, (int)sizeof(line));
    if (count < 0) {
        con_puts("readline: read failed\n");
        return 1;
    }
    con_puts("read: ");
    con_write(line, count);
    return 0;
}
