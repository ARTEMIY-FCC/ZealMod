/* clock.c — стрелки поверх цифр; длинное ▲ — выставить время. */
#include "game.h"
#include "wallclock.h"

#define CXX 120
#define CYY 104
#define R   84

static void hand(const band *b, int ang, int len, int w, px c)
{
    int s = fx_sin(ang), co = fx_cos(ang);
    int x = CXX + (len * s >> 14), y = CYY - (len * co >> 14);
    int px_ = -(w * co >> 15), py = -(w * s >> 15);
    gfx_line(b, CXX + px_, CYY + py, x, y, c);
    gfx_line(b, CXX - px_, CYY - py, x, y, c);
    gfx_line(b, CXX, CYY, x, y, c);
}

void run_clock(void)
{
    game_exit_button(BTN_DOWN);
    uint32_t last = now_ms();
    while (!game_quit()) {
        in_poll();
        if (in_hit() & B_UP) { wc_setup(); last = now_ms(); }
        wc_time t = wc_now();
        char s[64];
        fb_begin();
        for (band *b; (b = fb_next()); ) {
            gfx_vgrad(b, 0, 0, SCR_W, SCR_H, RGB(18, 20, 30), RGB(5, 5, 10));
            gfx_ring(b, CXX, CYY, R, 3, RGB(180, 190, 215));
            for (int i = 0; i < 60; i++) {
                int a = i * FX_TURN / 60;
                int r1 = i % 5 ? R - 7 : R - 13;
                int s1 = fx_sin(a), c1 = fx_cos(a);
                gfx_line(b, CXX + (r1 * s1 >> 14), CYY - (r1 * c1 >> 14),
                         CXX + ((R - 4) * s1 >> 14), CYY - ((R - 4) * c1 >> 14),
                         i % 5 ? RGB(70, 76, 95) : RGB(160, 170, 200));
            }
            int ah = (t.hour % 12) * FX_TURN / 12 + t.min * FX_TURN / 720;
            int am = t.min * FX_TURN / 60 + t.sec * FX_TURN / 3600;
            int as = t.sec * FX_TURN / 60;
            hand(b, ah, R - 36, 5, WHITE);
            hand(b, am, R - 18, 3, WHITE);
            hand(b, as, R - 12, 1, RGB(255, 90, 70));
            gfx_disc(b, CXX, CYY, 4, RGB(255, 90, 70));

            fx_fmt(s, sizeof s, "%02d:%02d", t.hour, t.min);
            gfx_text_c(b, 120, 212, &font_l, WHITE, s);
            if (t.valid) {
                fx_fmt(s, sizeof s, "%s, %d %s", wc_wday(t.wday), t.day, wc_month_gen(t.mon));
                gfx_text_c(b, 120, 232, &font_s, RGB(140, 150, 175), s);
            } else {
                gfx_text_c(b, 120, 232, &font_s, RGB(255, 170, 60), TR("▲ set the time", "▲ выставить время"));
            }
        }
        game_frame_wait(&last, 100);
    }
}
