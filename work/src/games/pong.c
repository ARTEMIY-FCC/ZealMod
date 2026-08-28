/* pong.c — на двоих (один вверх-вниз, второй влево-вправо) и против бота.
 * Всё в 16.16: у таймера нет FPU. */
#include "game.h"
#include "snd.h"

#define PW 6
#define PH 42
#define LX 10
#define RX (SCR_W - 10 - PW)
#define BALL 7
#define WIN 7
#define TOPBAR 0
#define VPS(v) ((int)((v) * 65536 / 30))   /* пикселей в секунду -> Q16 за кадр */

static int py1, py2;                /* 16.16 */
static int bx, by, bvx, bvy;
static int s1, s2, mode, serving, over;
static uint32_t serve_t, flash_l, flash_r;
static int trail_x[8], trail_y[8], trail_i;

static const px NEON_L = RGB(90, 220, 255), NEON_R = RGB(255, 140, 90);

static void serve(int to_right)
{
    bx = (SCR_W / 2 - BALL / 2) << 16;
    by = (SCR_H / 2 - BALL / 2) << 16;
    int a = 40 + (int)(plat_rand() % 80);            /* наклон подачи, пикс/с */
    if (plat_rand() & 1) a = -a;
    bvx = (to_right ? 1 : -1) * VPS(165);
    bvy = VPS(a);
    serving = 1; serve_t = now_ms();
}

static void reset(void)
{
    py1 = py2 = (SCR_H / 2 - PH / 2) << 16;
    s1 = s2 = 0; over = 0;
    serve(plat_rand() & 1);
}

static void bounce_paddle(int paddle_y, int dir)
{
    int rel = ((by >> 16) + BALL / 2) - ((paddle_y >> 16) + PH / 2);   /* -21..21 */
    bvy = VPS(rel * 9);
    bvx = dir * (iabs(bvx) + VPS(14));
    if (iabs(bvx) > VPS(430)) bvx = dir * VPS(430);
}

static void physics(int dt)
{
    if (serving) {
        if (now_ms() - serve_t < 700) return;
        serving = 0;
    }
    (void)dt;
    bx += bvx;
    by += bvy;
    if (by < 0) { by = -by; bvy = -bvy; }
    if ((by >> 16) + BALL > SCR_H) { by = (SCR_H - BALL) << 16; bvy = -bvy; }

    if (bvx < 0 && (bx >> 16) <= LX + PW && (bx >> 16) > LX - 8) {
        int y = by >> 16;
        if (y + BALL >= (py1 >> 16) && y <= (py1 >> 16) + PH) {
            bx = (LX + PW) << 16; bounce_paddle(py1, 1); flash_l = now_ms(); SND(snd_hit);
        }
    }
    if (bvx > 0 && (bx >> 16) + BALL >= RX && (bx >> 16) + BALL < RX + 14) {
        int y = by >> 16;
        if (y + BALL >= (py2 >> 16) && y <= (py2 >> 16) + PH) {
            bx = (RX - BALL) << 16; bounce_paddle(py2, -1); flash_r = now_ms(); SND(snd_hit);
        }
    }
    if ((bx >> 16) + BALL < 0) { s2++; SND(snd_hurt); if (s2 >= WIN) { over = 1; SND(snd_win); } else serve(0); }
    if ((bx >> 16) > SCR_W)    { s1++; SND(snd_pick); if (s1 >= WIN) { over = 1; SND(snd_win); } else serve(1); }

    trail_x[trail_i] = bx >> 16; trail_y[trail_i] = by >> 16;
    trail_i = (trail_i + 1) & 7;
}

static void ai(int dt)
{
    int target = (by >> 16) + BALL / 2 - PH / 2;
    int err = 26 - imin(s2 * 3, 18);                  /* бот слабеет, когда ведёт */
    if (bvx > 0) {
        int d = target - (py2 >> 16);
        if (iabs(d) > err) py2 += (d > 0 ? 1 : -1) * VPS(230);
    } else {
        int d = (SCR_H / 2 - PH / 2) - (py2 >> 16);
        if (iabs(d) > 20) py2 += (d > 0 ? 1 : -1) * VPS(90);
    }
}

static void draw(const band *b)
{
    char s[8];
    gfx_vgrad(b, 0, 0, SCR_W, SCR_H, RGB(8, 10, 18), RGB(2, 2, 6));
    for (int y = 4; y < SCR_H; y += 16) gfx_fill(b, SCR_W / 2 - 1, y, 3, 9, RGB(38, 42, 60));

    fx_fmt(s, sizeof s, "%d", s1);
    gfx_text_c(b, 70, 70, &font_xl, px_scale(NEON_L, 90), s);
    fx_fmt(s, sizeof s, "%d", s2);
    gfx_text_c(b, 170, 70, &font_xl, px_scale(NEON_R, 90), s);

    for (int i = 0; i < 8; i++) {
        int k = (trail_i + i) & 7;
        if (!trail_x[k] && !trail_y[k]) continue;
        gfx_round(b, trail_x[k], trail_y[k], BALL, BALL, 3, px_scale(WHITE, 20 + i * 14));
    }
    gfx_round(b, bx >> 16, by >> 16, BALL, BALL, 3, WHITE);

    px cl = now_ms() - flash_l < 90 ? WHITE : NEON_L;
    px cr = now_ms() - flash_r < 90 ? WHITE : NEON_R;
    gfx_round(b, LX, py1 >> 16, PW, PH, 3, cl);
    gfx_round(b, RX, py2 >> 16, PW, PH, 3, cr);
    gfx_fill(b, LX - 2, (py1 >> 16) + 4, 1, PH - 8, px_scale(cl, 120));
    gfx_fill(b, RX + PW + 1, (py2 >> 16) + 4, 1, PH - 8, px_scale(cr, 120));

    if (serving) gfx_text_c(b, SCR_W / 2, 210, &font_s, RGB(120, 130, 150),
                            mode ? TR("▲▼ and ◀▶", "▲▼ и ◀▶")
                                 : TR("▲▼ — you", "▲▼ — вы"));
    if (over) {
        const char *w = s1 >= WIN
            ? (mode ? TR("Left wins", "Слева победил") : TR("You win", "Вы выиграли"))
            : (mode ? TR("Right wins", "Справа победил") : TR("Computer wins", "Бот выиграл"));
        game_banner(b, w, TR("▶ again", "▶ ещё раз"), s1 >= WIN ? NEON_L : NEON_R);
    }
}

static int pick_mode(void)
{
    int sel = 0;
    uint32_t last = now_ms();
    for (;;) {
        in_poll();
        if (game_quit()) return -1;
        if (in_hit() & (B_LEFT | B_RIGHT)) sel ^= 1;
        if (in_hit() & B_UP) return sel;
        fb_begin();
        for (band *b; (b = fb_next()); ) {
            gfx_vgrad(b, 0, 0, SCR_W, SCR_H, RGB(8, 10, 18), RGB(2, 2, 6));
            gfx_text_c(b, 120, 60, &font_l, WHITE, "PONG");
            for (int i = 0; i < 2; i++) {
                px c = i == sel ? RGB(90, 220, 255) : RGB(60, 64, 80);
                gfx_frame(b, 30, 100 + i * 46, 180, 38, 2, c);
                gfx_text_c(b, 120, 124 + i * 46, &font_m, i == sel ? WHITE : RGB(140, 145, 160),
                           i ? TR("two players", "вдвоём")
                             : TR("vs computer", "против бота"));
            }
            gfx_text_c(b, 120, 206, &font_s, RGB(110, 118, 140), TR("◀▶ choose   ▲ start", "◀▶ выбрать   ▲ начать"));
        }
        game_frame_wait(&last, 33);
    }
}

void run_pong(void)
{
    game_exit_button(BTN_LEFT);
    mode = pick_mode();
    if (mode < 0) return;
    reset();
    uint32_t last = now_ms();
    while (!game_quit()) {
        in_poll();
        uint32_t h = in_held();
        int dt = 33;
        if (over) {
            if (in_hit() & B_RIGHT) reset();
        } else {
            int sp = VPS(300);
            if (h & B_UP) py1 -= sp;
            if (h & B_DOWN) py1 += sp;
            if (mode) {
                if (h & B_LEFT) py2 -= sp;
                if (h & B_RIGHT) py2 += sp;
            } else ai(dt);
            if (py1 < 0) py1 = 0;
            if (py2 < 0) py2 = 0;
            if ((py1 >> 16) + PH > SCR_H) py1 = (SCR_H - PH) << 16;
            if ((py2 >> 16) + PH > SCR_H) py2 = (SCR_H - PH) << 16;
            physics(dt);
        }
        fb_begin();
        for (band *b; (b = fb_next()); ) draw(b);
        game_frame_wait(&last, 33);
    }
}
