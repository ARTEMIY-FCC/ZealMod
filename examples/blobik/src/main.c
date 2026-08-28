/* blobik — программа для ZealMod.
 *
 * Всё, что можно звать из модуля, объявлено в plat.h, game.h, fx.h и snd.h.
 * Правила простые:
 *   - изменяемых данных с начальным значением быть не должно (их некому
 *     копировать в ОЗУ): либо const, либо обнулённые;
 *   - память берётся у mem_alloc(), она обнулена и живёт до выхода;
 *   - кадр рисуется в fb_begin()/fb_next(), между кадрами обязателен
 *     game_frame_wait(), иначе часы решат, что мод завис.
 */
#include "plat.h"
#include "game.h"
#include "fx.h"
#include "snd.h"

void zm_main(void)
{
    game_exit_button(BTN_UP);        /* этой кнопкой выходят: держать 1,4 с */
    int x = SCR_W / 2, y = SCR_H / 2;
    uint32_t last = now_ms();

    while (!game_quit()) {
        in_poll();
        uint32_t hit = in_rep();
        if (hit & B_LEFT)  x -= 8;
        if (hit & B_RIGHT) x += 8;
        if (hit & B_DOWN)  y += 8;
        x = iclamp(x, 20, SCR_W - 20);
        y = iclamp(y, 20, SCR_H - 20);
        if (hit) SND(snd_click);

        fb_begin();
        for (band *b; (b = fb_next()); ) {
            gfx_clear(b, RGB(12, 14, 22));
            gfx_disc(b, x, y, 16, RGB(110, 168, 255));
            gfx_text_c(b, SCR_W / 2, 36, &font_m, WHITE, "blobik");
            gfx_text_c(b, SCR_W / 2, 214, &font_s, RGB(140, 146, 162),
                       "◀ ▶ двигать, ▲ держать — выход");
        }
        game_frame_wait(&last, 33);   /* ~30 кадров в секунду */
    }
}
