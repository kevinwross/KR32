#pragma once

// Guest display declarations. Carries the same declarations as
// kGuestDisplayHeader in include/kr32/cc/guest_headers.h; see fb.h for why
// both copies exist.

typedef struct display_surface {
    unsigned int offset;
    int width;
    int height;
    int stride;
} display_surface;
int sys_display_present(void);
