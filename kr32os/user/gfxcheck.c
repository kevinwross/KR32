/* Checks clipping, fills, and a line: a rectangle clipped to 100x100, a blue
   square outside it, and a green diagonal. */

#include <kr32/con.h>
#include <kr32/gfx.h>
#include <kr32/sys.h>

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
    /* Block until a key or a serial byte arrives, so the frame stays visible for
       as long as the user wants it. The screenshot harness sends neither, so it
       samples this frame; the dismiss test sends a byte instead. */
    char key = 0;
    (void)sys_read(KR32_STDIN, &key, 1);
    con_hide(0);
    return 0;
}
