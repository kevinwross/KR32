#pragma once

void process_init(void);
int process_spawn(const char *name);
int process_exit(int pid, int status);
void process_list(void);
