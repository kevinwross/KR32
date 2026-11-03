#pragma once

// KR32 freestanding C++ runtime declarations.
//
// The compiler injects these declarations for `#include <kr32/runtime.hpp>`;
// this header is the user-facing copy.

void exit(int code);
void* malloc(int size);
void free(void* pointer);
void* memcpy(void* destination, const void* source, int count);
void* memset(void* destination, int value, int count);
int strlen(const char* text);
