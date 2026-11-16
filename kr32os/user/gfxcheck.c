/* Checks clipping, fills, and a line: a rectangle clipped to 100x100, a blue
   square outside it, and a green diagonal. */

#include <kr32/con.h>
#include <kr32/gfx.h>

typedef struct kr32_event_user {
    unsigned char ascii;
    unsigned char modifiers;
    unsigned char scancode;
    unsigned char type;
} kr32_event;

int input_pending(void);
int input_poll(kr32_event *event);
void con_hide(int hidden);

int main(void) {
    gfx_surface *screen = gfx_create(800, 600);
    if (screen == 0) {
        con_puts("gfxcheck: no surface\n");
        return 1;
    }
    con_hide(1);
    gfx_clear(screen, 0x00000000U);
    gfx_set_clip(screen, 100, 100, 100, 100);
    gfx_fill_rect(screen, 0, 0, 800, 600, 0x00FF0000U);
    gfx_reset_clip(screen);
    gfx_fill_rect(screen, 400, 400, 50, 50, 0x000000FFU);
    gfx_line(screen, 0, 599, 799, 0, 0x0000FF00U);
    gfx_present(screen, 0, 0, 800, 600);
    /* Wait for a key so the frame is visible for as long as the user wants it.
       The screenshot harness sends no key, so it samples this frame. */
    while (input_pending() == 0) {
    }
    // Drain the key press and its release so neither reaches the shell prompt.
    kr32_event event;
    while (input_poll(&event) != 0) {
    }
    con_hide(0);
    return 0;
}
