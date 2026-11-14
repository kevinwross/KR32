/* Draws a simple scene through the host graphics commands and presents it.
   The console is hidden first so the terminal does not paint over the frame;
   it stays hidden after the demo so the drawing remains visible. */

#include <kr32/con.h>
#include <kr32/gfx.h>

int main(void) {
    gfx_surface *screen = gfx_create(800, 600);
    if (screen == 0) {
        con_puts("gfxdemo: no surface\n");
        return 1;
    }
    con_hide(1);
    gfx_clear(screen, 0x00101820U);
    gfx_fill_rect(screen, 100, 100, 200, 120, 0x0030A030U);
    gfx_frame_rect(screen, 320, 100, 200, 120, 0x00E6EDF3U);
    gfx_line(screen, 100, 260, 700, 260, 0x00F06060U);
    gfx_text(screen, 100, 300, "KR32 gfx demo", 0x00E6EDF3U, 0x00101820U);
    gfx_present(screen, 0, 0, 800, 600);
    /* The screenshot harness samples at a fixed step count, so stay on this
       frame with the console hidden instead of exiting into the shell prompt. */
    for (volatile int keep = 1; keep; ) {
    }
    return 0;
}