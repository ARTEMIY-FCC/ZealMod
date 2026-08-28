#include "game.h"
#include "apps/wallclock.h"
#include "snd.h"
#include "nvram.h"

#ifdef PLAT_HOST
static uint32_t hiscore[HI_N];
#else
__attribute__((section(".persist"))) static uint32_t hiscore[HI_N];
#endif

/* база слотов текущего модуля: hi_get(0) у каждого своя */
static int hi_base;
void zm_set_slot(int mod) { hi_base = (unsigned)mod < 32 ? mod * HI_PER_MOD : 0; }
static int hi_at(int s) { return hi_base + ((unsigned)s < HI_PER_MOD ? s : 0); }

uint32_t hi_get(int s) { return hiscore[hi_at(s)]; }
void hi_set(int s, uint32_t v)
{
    int i = hi_at(s);
    if (v > hiscore[i]) { hiscore[i] = v; nv_dirty(); }
}

uint32_t hi_raw(int s) { return (unsigned)s < HI_N ? hiscore[s] : 0; }
void hi_put(int s, uint32_t v) { if ((unsigned)s < HI_N) hiscore[s] = v; }

#ifndef PLAT_HOST
extern volatile int zg_cmd_back;
#else
static int zg_cmd_back;
#endif

static int exit_btn, exit_hold;

void game_exit_button(int b) { exit_btn = b; exit_hold = EXIT_HOLD_MS; }
void game_exit_defaults(void) { exit_btn = BTN_UP; exit_hold = EXIT_HOLD_MS; }
void game_exit_hold(int ms) { exit_hold = ms; }

int game_quit(void)
{
    if (zg_cmd_back) { zg_cmd_back = 0; return 1; }
    return (int)in_held_ms(exit_btn) > exit_hold;
}

void game_frame_wait(uint32_t *last, int ms)
{
    wc_tick();
    snd_update();
    uint32_t now = now_ms(), dt = now - *last;
    /* заснуть обязаны в любом случае: иначе задача съедает всё время и
     * сторожевой таймер прибивает систему за голодающий IDLE */
    /* уступаем побольше, если кадр тяжёлый: иначе IDLE голодает и ругается сторож */
    plat_sleep_ms((int)dt < ms ? ms - dt : ((int)dt > 50 ? 10 : 1));
    *last = now_ms();
}

void game_banner(const band *b, const char *big, const char *small, px tint)
{
    gfx_dim(b, 0, 78, SCR_W, 84, 90);
    gfx_fill(b, 0, 78, SCR_W, 2, tint);
    gfx_fill(b, 0, 160, SCR_W, 2, tint);
    gfx_text_c(b, SCR_W / 2, 122, &font_l, WHITE, big);
    if (small) gfx_text_c(b, SCR_W / 2, 148, &font_s, RGB(190, 195, 210), small);
}

int game_confirm(const char *text)
{
    uint32_t last = now_ms();
    for (;;) {
        in_poll();
        if (in_hit() & B_RIGHT) return 1;
        if (in_hit() & B_LEFT) return 0;
        fb_begin();
        for (band *b; (b = fb_next()); ) {
            gfx_clear(b, RGB(10, 10, 16));
            gfx_text_c(b, 120, 110, &font_m, WHITE, text);
            gfx_text_c(b, 120, 150, &font_s, RGB(150, 155, 170), TR("◀ no     yes ▶", "◀ нет     да ▶"));
        }
        game_frame_wait(&last, 33);
    }
}
