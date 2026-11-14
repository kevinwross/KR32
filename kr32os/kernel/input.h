#pragma once

/* Keyboard access for user programs. */

struct kr32_event {
    unsigned char ascii;
    unsigned char modifiers;
    unsigned char scancode;
    unsigned char type;
};

/* 1 when the keyboard has a queued event. */
int input_pending(void);

/* Pops one event into the four bytes at a user address. Returns 1 when an
   event was written, 0 when the queue is empty, or a negative errno when the
   pointer does not validate. */
int input_poll_from_user(unsigned int address);
