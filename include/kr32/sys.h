#pragma once

// Guest syscall declarations. Carries the same declarations as kGuestSysHeader
// in include/kr32/cc/guest_headers.h; see fb.h for why both copies exist.

enum { KR32_STDIN = 0, KR32_STDOUT = 1, KR32_STDERR = 2 };
int sys_write(int descriptor, const void *buffer, int length);
int sys_read(int descriptor, char *buffer, int length);
int sys_getpid(void);
int sys_yield(void);
int sys_brk(unsigned int address);
void sys_exit(int status);
void sys_halt(void);
void sys_shutdown(int status);
