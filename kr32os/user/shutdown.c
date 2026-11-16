/* Exits the machine through the shutdown syscall so the host process returns
   the program's status. The exit-code test runs this and checks that status. */

#include <kr32/con.h>

void sys_shutdown(int status);

int main(void) {
    con_puts("shutdown: exiting with status 7\n");
    sys_shutdown(7);
    return 0;
}
