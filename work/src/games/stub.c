/* временные заглушки — заменяются по мере готовности игр */
#include "game.h"

static void stub(const char *name)
{
    uint32_t last = now_ms();
    while (!game_quit()) {
        in_poll();
        fb_begin();
        for (band *b; (b = fb_next()); ) {
            gfx_clear(b, RGB(12, 12, 18));
            gfx_text_c(b, 120, 110, &font_l, WHITE, name);
            gfx_text_c(b, 120, 140, &font_s, RGB(150, 150, 165), TR("work in progress", "ещё пишется"));
        }
        game_frame_wait(&last, 33);
    }
}

#define STUB(fn, name) void fn(void) { stub(name); }
