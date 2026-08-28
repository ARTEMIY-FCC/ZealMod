/* snake.c — змейка: скруглённые звенья, глаза смотрят по ходу, яблоко с бликом. */
#include "game.h"
#include "snd.h"

#define CELL 15
#define COLS 16
#define ROWS 14
#define TOP  30
#define MAXLEN (COLS * ROWS)

static uint8_t sx[MAXLEN], sy[MAXLEN];
static int head, len, dir, ndir, ax, ay, score, best, dead, grow;
static uint32_t step_ms;

static const px BODY_A = RGB(126, 217, 87), BODY_B = RGB(60, 160, 60);
static const px BG1 = RGB(24, 46, 22), BG2 = RGB(16, 32, 16);

static void place_apple(void)
{
    for (;;) {
        int x = plat_rand() % COLS, y = plat_rand() % ROWS, ok = 1;
        for (int i = 0; i < len; i++) {
            int k = (head - i + MAXLEN) % MAXLEN;
            if (sx[k] == x && sy[k] == y) { ok = 0; break; }
        }
        if (ok) { ax = x; ay = y; return; }
    }
}

static void reset(void)
{
    len = 4; head = 0; dir = ndir = 3; score = 0; dead = 0; grow = 0; step_ms = 170;
    for (int i = 0; i < len; i++) { sx[(head - i + MAXLEN) % MAXLEN] = (uint8_t)(4 - i); sy[(head - i + MAXLEN) % MAXLEN] = 7; }
    place_apple();
}

static void step(void)
{
    static const int8_t dx[4] = { 0, 0, -1, 1 }, dy[4] = { -1, 1, 0, 0 };
    dir = ndir;
    int nx = sx[head] + dx[dir], ny = sy[head] + dy[dir];
    if (nx < 0 || ny < 0 || nx >= COLS || ny >= ROWS) { dead = 1; SND(snd_die); return; }
    for (int i = 0; i < len - 1; i++) {
        int k = (head - i + MAXLEN) % MAXLEN;
        if (sx[k] == nx && sy[k] == ny) { dead = 1; SND(snd_die); return; }
    }
    head = (head + 1) % MAXLEN;
    sx[head] = (uint8_t)nx; sy[head] = (uint8_t)ny;
    if (nx == ax && ny == ay) {
        score += 10; grow += 2; place_apple(); SND(snd_pick);
        if (step_ms > 70) step_ms -= 4;
    }
    if (grow) { grow--; len++; }
}

static void draw(const band *b)
{
    char s[16];
    gfx_vgrad(b, 0, TOP, SCR_W, ROWS * CELL, BG1, BG2);
    for (int i = 0; i <= COLS; i++) gfx_vline(b, i * CELL, TOP, ROWS * CELL, RGB(20, 40, 20));
    for (int i = 0; i <= ROWS; i++) gfx_hline(b, 0, TOP + i * CELL, SCR_W, RGB(20, 40, 20));

    gfx_fill(b, 0, 0, SCR_W, TOP, RGB(10, 20, 10));
    gfx_text(b, 8, 21, &font_m, RGB(126, 217, 87), TR("SNAKE", "ЗМЕЙКА"));
    fx_fmt(s, sizeof s, "%d", score);
    gfx_text(b, 150, 21, &font_m, WHITE, s);
    fx_fmt(s, sizeof s, TR("best %d", "рек %d"), best);
    gfx_text(b, 186, 20, &font_s, RGB(120, 150, 120), s);

    /* яблоко */
    int px_ = ax * CELL + CELL / 2, py = TOP + ay * CELL + CELL / 2;
    gfx_disc(b, px_, py + 1, 6, RGB(200, 40, 36));
    gfx_disc(b, px_ - 2, py - 1, 2, RGB(255, 150, 140));
    gfx_fill(b, px_, py - 7, 2, 3, RGB(90, 60, 30));
    gfx_fill(b, px_ + 2, py - 8, 4, 2, RGB(80, 190, 80));

    for (int i = 0; i < len; i++) {
        int k = (head - i + MAXLEN) % MAXLEN;
        int x = sx[k] * CELL, y = TOP + sy[k] * CELL;
        px c = px_mix(BODY_A, BODY_B, i * 256 / (len > 1 ? len : 1));
        gfx_round(b, x + 1, y + 1, CELL - 2, CELL - 2, i ? 4 : 5, c);
        if (i + 1 < len) {                          /* перемычка к следующему звену */
            int p = (head - i - 1 + MAXLEN) % MAXLEN;
            int ddx = sx[p] - sx[k], ddy = sy[p] - sy[k];
            if (ddx > 0)      gfx_fill(b, x + CELL - 4, y + 3, 6, CELL - 6, c);
            else if (ddx < 0) gfx_fill(b, x - 2, y + 3, 6, CELL - 6, c);
            else if (ddy > 0) gfx_fill(b, x + 3, y + CELL - 4, CELL - 6, 6, c);
            else if (ddy < 0) gfx_fill(b, x + 3, y - 2, CELL - 6, 6, c);
        }
        if (!i) {                                   /* глаза */
            static const int8_t ex[4][2] = { { -3, 3 }, { -3, 3 }, { -2, -2 }, { 2, 2 } };
            static const int8_t ey[4][2] = { { -2, -2 }, { 2, 2 }, { -3, 3 }, { -3, 3 } };
            for (int e = 0; e < 2; e++) {
                gfx_disc(b, x + CELL / 2 + ex[dir][e], y + CELL / 2 + ey[dir][e], 2, WHITE);
                gfx_disc(b, x + CELL / 2 + ex[dir][e], y + CELL / 2 + ey[dir][e], 1, RGB(20, 20, 20));
            }
        }
    }
    if (dead) game_banner(b, TR("You ate yourself", "Съел сам себя"),
                        TR("▶ again", "▶ заново"), RGB(200, 60, 50));
}

void run_snake(void)
{
    game_exit_button(BTN_UP);
    best = (int)hi_get(0);
    reset();
    uint32_t last = now_ms(), tick = now_ms();
    while (!game_quit()) {
        in_poll();
        uint32_t h = in_hit();
        if (!dead) {
            if ((h & B_UP) && dir != 1) ndir = 0;
            if ((h & B_DOWN) && dir != 0) ndir = 1;
            if ((h & B_LEFT) && dir != 3) ndir = 2;
            if ((h & B_RIGHT) && dir != 2) ndir = 3;
            if (now_ms() - tick >= step_ms) { tick = now_ms(); step(); }
            if (dead) { hi_set(0, (uint32_t)score); best = (int)hi_get(0); }
        } else if (h & B_RIGHT) reset();
        fb_begin();
        for (band *b; (b = fb_next()); ) draw(b);
        game_frame_wait(&last, 33);
    }
}
