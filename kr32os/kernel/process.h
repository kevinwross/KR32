#pragma once

void process_init(void);
int process_spawn(const char *name);
int process_exit(int pid, int status);
int process_reap(int pid);
void process_list(void);
// The pid of the process the kernel is running, or 0 while it runs the shell.
void process_set_current(int pid);
int process_current(void);
