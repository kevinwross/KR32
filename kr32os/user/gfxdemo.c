/* Draws a simple scene through the host graphics commands and presents it.
   The console is hidden first so the terminal does not paint over the frame.
   The frame stays on screen until a key arrives; then the console comes back
   and the program returns through the normal exit path, so the shell reports a
   status and keeps running instead of the machine halting. */

#include <kr32/con.h>
#include <kr32/gfx.h>
#include <kr32/sys.h>

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
    /* Block until a key or a serial byte arrives, so the frame stays visible for
       as long as the user wants it. The screenshot harness sends neither, so it
       samples this frame; the dismiss test sends a byte instead. */
    char key = 0;
    (void)sys_read(KR32_STDIN, &key, 1);
    con_hide(0);
    return 0;
}
