#pragma once

// Guest input declarations. Carries the same declarations as
// kGuestInputHeader in include/kr32/cc/guest_headers.h; see fb.h for why both
// copies exist.

typedef struct kr32_event {
    unsigned char ascii;
    unsigned char modifiers;
    unsigned char scancode;
    unsigned char type;
} kr32_event;
int input_pending(void);
int input_poll(kr32_event *event);
