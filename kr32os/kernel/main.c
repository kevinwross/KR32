#include "console.h"
#include "timer.h"

void shell_run(void);
void traps_install(void);
void kernel_enable_interrupts(void);

void kmain(void) {
    console_write("KR32OS M3\n");
    traps_install();
    timer_init();
    kernel_enable_interrupts();
    shell_run();
}
