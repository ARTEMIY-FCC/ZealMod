/* calendar.c — месяц целиком, сегодня обведено; ◀▶ листает. */
#include "game.h"
#include "wallclock.h"

#define CW 32
#define CH 26
#define GX 8
#define GY 62

void run_calendar(void)
{
    game_exit_button(BTN_DOWN);
    wc_time t = wc_now();
    int y = t.valid ? t.year : 2026, m = t.valid ? t.mon : 0;
    uint32_t last = now_ms();
    while (!game_quit()) {
        in_poll();
        uint32_t h = in_rep();
        if (h & B_RIGHT) { if (++m > 11) { m = 0; y++; } }
        if (h & B_LEFT)  { if (--m < 0) { m = 11; y--; } }
        if (in_hit() & B_UP) { t = wc_now(); if (t.valid) { y = t.year; m = t.mon; } }
        t = wc_now();
        uint32_t first = wc_days_from_civil(y, m, 1);
        int wd = (int)((first + 5) % 7);
        static const uint8_t md[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
        int n = md[m];
        if (m == 1 && ((!(y % 4) && (y % 100)) || !(y % 400))) n = 29;
        char s[48];
        fb_begin();
        for (band *b; (b = fb_next()); ) {
            gfx_vgrad(b, 0, 0, SCR_W, SCR_H, RGB(248, 248, 252), RGB(214, 218, 230));
            gfx_fill(b, 0, 0, SCR_W, 44, RGB(206, 60, 60));
            fx_fmt(s, sizeof s, "%s %d", wc_month(m), y);
            gfx_text_c(b, 120, 29, &font_m, WHITE, s);
            for (int i = 0; i < 7; i++)
                gfx_text_c(b, GX + i * CW + CW / 2, 56, &font_s,
                           i >= 5 ? RGB(200, 80, 80) : RGB(110, 115, 130), wc_wday(i));
            for (int d = 1; d <= n; d++) {
                int k = wd + d - 1, col = k % 7, row = k / 7;
                int x = GX + col * CW, yy = GY + row * CH;
                int today = t.valid && t.year == y && t.mon == m && t.day == d;
                if (today) gfx_round(b, x + 2, yy - 1, CW - 4, CH - 3, 5, RGB(206, 60, 60));
                fx_fmt(s, sizeof s, "%d", d);
                gfx_text_c(b, x + CW / 2, yy + 15, &font_m,
                           today ? WHITE : (col >= 5 ? RGB(200, 80, 80) : RGB(50, 52, 62)), s);
            }
            gfx_text_c(b, 120, 234, &font_s, RGB(120, 126, 140), TR("◀▶ month   ▲ today", "◀▶ месяц   ▲ сегодня"));
        }
        game_frame_wait(&last, 40);
    }
}
