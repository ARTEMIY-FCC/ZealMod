/* tetris.c — семь фигур в канонических цветах, тень падения, счёт по строкам. */
#include "game.h"
#include "snd.h"

#define COLS 10
#define ROWS 18
#define CELL 12
#define BX   6
#define BY   14

static const uint16_t shapes[7][4] = {   /* по 4 поворота, битовая маска 4x4 */
    { 0x0F00, 0x2222, 0x00F0, 0x4444 },  /* I */
    { 0x0660, 0x0660, 0x0660, 0x0660 },  /* O */
    { 0x0E40, 0x4C40, 0x4E00, 0x4640 },  /* T */
    { 0x06C0, 0x8C40, 0x6C00, 0x4620 },  /* S */
    { 0x0C60, 0x4C80, 0xC600, 0x2640 },  /* Z */
    { 0x08E0, 0x44C0, 0x0E20, 0x6440 },  /* J */
    { 0x02E0, 0xC440, 0x0E80, 0x4460 },  /* L */
};
static const px pcol[7] = {
    RGB(0, 240, 240), RGB(240, 240, 0), RGB(160, 0, 240), RGB(0, 240, 0),
    RGB(240, 0, 0), RGB(0, 80, 240), RGB(240, 160, 0),
};

static uint8_t bd[ROWS][COLS];
static int cur, rot, cx, cy, nxt, score, lines, level, over, best;
static uint32_t drop_ms, drop_t;
static int flash_rows[4], n_flash;
static uint32_t flash_t;

static int cellat(int p, int r, int i, int j) { return (shapes[p][r] >> (15 - (i * 4 + j))) & 1; }

static int collide(int p, int r, int x, int y)
{
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            if (!cellat(p, r, i, j)) continue;
            int bx = x + j, by = y + i;
            if (bx < 0 || bx >= COLS || by >= ROWS) return 1;
            if (by >= 0 && bd[by][bx]) return 1;
        }
    return 0;
}

static void spawn(void)
{
    cur = nxt; nxt = (int)(plat_rand() % 7);
    rot = 0; cx = 3; cy = -2;
    if (collide(cur, rot, cx, cy)) { over = 1; SND(snd_die); }
}

static void reset(void)
{
    memset(bd, 0, sizeof bd);
    score = lines = 0; level = 1; over = 0; n_flash = 0;
    drop_ms = 600; drop_t = now_ms();
    nxt = (int)(plat_rand() % 7);
    spawn();
}

static void lock_piece(void)
{
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            if (cellat(cur, rot, i, j) && cy + i >= 0) bd[cy + i][cx + j] = (uint8_t)(cur + 1);
    n_flash = 0;
    for (int r = 0; r < ROWS; r++) {
        int full = 1;
        for (int c = 0; c < COLS; c++) if (!bd[r][c]) { full = 0; break; }
        if (full) flash_rows[n_flash++] = r;
    }
    if (n_flash) { flash_t = now_ms(); SND(snd_win); return; }
    spawn();
}

static void clear_rows(void)
{
    for (int k = 0; k < n_flash; k++) {
        int r = flash_rows[k];
        for (int y = r; y > 0; y--) memcpy(bd[y], bd[y - 1], COLS);
        memset(bd[0], 0, COLS);
    }
    static const int pts[5] = { 0, 100, 300, 500, 800 };
    score += pts[n_flash] * level;
    lines += n_flash;
    level = 1 + lines / 10;
    drop_ms = 600 - (level - 1) * 45;
    if ((int)drop_ms < 90) drop_ms = 90;
    n_flash = 0;
    spawn();
}

static void block(const band *b, int x, int y, px c, int ghost)
{
    if (ghost) { gfx_frame(b, x, y, CELL, CELL, 1, px_scale(c, 130)); return; }
    gfx_fill(b, x, y, CELL, CELL, c);
    gfx_fill(b, x, y, CELL, 2, px_mix(c, WHITE, 110));
    gfx_fill(b, x, y, 2, CELL, px_mix(c, WHITE, 70));
    gfx_fill(b, x, y + CELL - 2, CELL, 2, px_scale(c, 130));
    gfx_fill(b, x + CELL - 2, y, 2, CELL, px_scale(c, 150));
}

static void draw(const band *b)
{
    char s[48];
    gfx_vgrad(b, 0, 0, SCR_W, SCR_H, RGB(12, 14, 32), RGB(4, 4, 12));
    gfx_frame(b, BX - 2, BY - 2, COLS * CELL + 4, ROWS * CELL + 4, 2, RGB(70, 80, 130));
    gfx_fill(b, BX, BY, COLS * CELL, ROWS * CELL, RGB(8, 8, 18));
    for (int i = 1; i < COLS; i++) gfx_vline(b, BX + i * CELL, BY, ROWS * CELL, RGB(18, 18, 34));
    for (int i = 1; i < ROWS; i++) gfx_hline(b, BX, BY + i * CELL, COLS * CELL, RGB(18, 18, 34));

    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            if (bd[r][c]) block(b, BX + c * CELL, BY + r * CELL, pcol[bd[r][c] - 1], 0);

    if (!over && !n_flash) {
        int gy = cy;                                     /* тень */
        while (!collide(cur, rot, cx, gy + 1)) gy++;
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++) {
                if (!cellat(cur, rot, i, j)) continue;
                if (gy + i >= 0) block(b, BX + (cx + j) * CELL, BY + (gy + i) * CELL, pcol[cur], 1);
                if (cy + i >= 0) block(b, BX + (cx + j) * CELL, BY + (cy + i) * CELL, pcol[cur], 0);
            }
    }
    if (n_flash) {
        int on = ((now_ms() - flash_t) / 60) & 1;
        for (int k = 0; k < n_flash; k++)
            gfx_fill(b, BX, BY + flash_rows[k] * CELL, COLS * CELL, CELL,
                     on ? WHITE : RGB(200, 200, 255));
    }

    int px_ = BX + COLS * CELL + 12;
    gfx_text(b, px_, 26, &font_m, RGB(120, 200, 255), TR("TETRIS", "ТЕТРИС"));
    gfx_text(b, px_, 52, &font_s, RGB(140, 145, 170), TR("score", "счёт"));
    fx_fmt(s, sizeof s, "%d", score); gfx_text(b, px_, 70, &font_m, WHITE, s);
    gfx_text(b, px_, 96, &font_s, RGB(140, 145, 170), TR("lines", "строк"));
    fx_fmt(s, sizeof s, "%d", lines); gfx_text(b, px_, 114, &font_m, WHITE, s);
    gfx_text(b, px_, 140, &font_s, RGB(140, 145, 170), TR("level", "уровень"));
    fx_fmt(s, sizeof s, "%d", level); gfx_text(b, px_, 158, &font_m, WHITE, s);
    gfx_text(b, px_, 184, &font_s, RGB(140, 145, 170), TR("next", "дальше"));
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            if (cellat(nxt, 0, i, j)) block(b, px_ + j * 10, 192 + i * 10, pcol[nxt], 0);
    if (over) {
        fx_fmt(s, sizeof s, TR("score %d  best %d", "счёт %d  рекорд %d"), score, best);
        game_banner(b, TR("Stack full", "Стакан полон"), s, RGB(240, 80, 60));
    }
}

void run_tetris(void)
{
    game_exit_button(BTN_UP);
    best = (int)hi_get(0);
    reset();
    uint32_t last = now_ms();
    while (!game_quit()) {
        in_poll();
        uint32_t h = in_hit(), r = in_rep();
        if (n_flash) {
            if (now_ms() - flash_t > 240) clear_rows();
        } else if (over) {
            if (h & B_RIGHT) reset();
        } else {
            if ((r & B_LEFT) && !collide(cur, rot, cx - 1, cy)) cx--;
            if ((r & B_RIGHT) && !collide(cur, rot, cx + 1, cy)) cx++;
            if (h & B_UP) {
                int nr = (rot + 1) & 3;
                if (!collide(cur, nr, cx, cy)) { rot = nr; SND(snd_click); }
                else if (!collide(cur, nr, cx - 1, cy)) { cx--; rot = nr; }
                else if (!collide(cur, nr, cx + 1, cy)) { cx++; rot = nr; }
            }
            uint32_t speed = (in_held() & B_DOWN) ? 40 : drop_ms;
            if (now_ms() - drop_t >= speed) {
                drop_t = now_ms();
                if (!collide(cur, rot, cx, cy + 1)) cy++;
                else lock_piece();
                if (in_held() & B_DOWN) score += 1;
            }
            if (over) { hi_set(0, (uint32_t)score); best = (int)hi_get(0); }
        }
        fb_begin();
        for (band *b; (b = fb_next()); ) draw(b);
        game_frame_wait(&last, 33);
    }
    hi_set(0, (uint32_t)score);
}
