/* g2048.c — 2048 в родных цветах, с проездом плиток и всплытием новой. */
#include "game.h"
#include "snd.h"

#define N     4
#define TILE  44
#define GAP   4
#define BX    22
#define BY    44
#define SLIDE_MS 110
#define POP_MS   90

static const px tile_col[] = {
    RGB(205, 193, 180),                    /* пусто */
    RGB(238, 228, 218), RGB(237, 224, 200), RGB(242, 177, 121), RGB(245, 149, 99),
    RGB(246, 124, 95),  RGB(246, 94, 59),  RGB(237, 207, 114), RGB(237, 204, 97),
    RGB(237, 200, 80),  RGB(237, 197, 63), RGB(237, 194, 46),  RGB(60, 58, 50),
};
#define NCOL ((int)(sizeof tile_col / sizeof *tile_col))
#define DARK_TXT RGB(119, 110, 101)
#define LITE_TXT RGB(249, 246, 242)
#define BOARD_BG RGB(187, 173, 160)
#define PAGE_BG  RGB(250, 248, 239)

typedef struct { int8_t v, fr, fc, tr, tc; uint8_t merged; } move_t;

static int8_t  bd[N][N];
static move_t  mv[N * N];
static int     n_mv, score, best, over, won;
static uint32_t slide_t0, pop_t0;
static int8_t  pop_r, pop_c;

static int cellx(int c) { return BX + GAP + c * (TILE + GAP); }
static int celly(int r) { return BY + GAP + r * (TILE + GAP); }

static void spawn(void)
{
    int free_[N * N], n = 0;
    for (int r = 0; r < N; r++)
        for (int c = 0; c < N; c++)
            if (!bd[r][c]) free_[n++] = r * N + c;
    if (!n) return;
    int k = free_[plat_rand() % n];
    bd[k / N][k % N] = (plat_rand() & 15) ? 1 : 2;
    pop_r = k / N; pop_c = k % N; pop_t0 = now_ms();
}

static void reset(void)
{
    memset(bd, 0, sizeof bd);
    score = 0; over = 0; won = 0; n_mv = 0;
    spawn(); spawn();
}

/* сдвиг одной линии; get/put разворачивают доску под нужное направление */
static int slide_line(int8_t *line, int idx[N], int dir_r, int dir_c)
{
    int vals[N], srci[N], n = 0, moved = 0;
    for (int i = 0; i < N; i++) if (line[i]) { vals[n] = line[i]; srci[n] = i; n++; }
    int8_t outv[N] = { 0, 0, 0, 0 };
    int out = 0;
    for (int i = 0; i < n; ) {
        int dst = out;
        if (i + 1 < n && vals[i] == vals[i + 1]) {
            outv[out] = (int8_t)(vals[i] + 1);
            score += 1 << (vals[i] + 1);
            if (vals[i] + 1 >= 11) won = 1;
            for (int k = 0; k < 2; k++) {
                move_t *m = &mv[n_mv++];
                m->v = (int8_t)vals[i]; m->merged = k;
                m->fr = (int8_t)(idx[srci[i + k]] / N); m->fc = (int8_t)(idx[srci[i + k]] % N);
                m->tr = (int8_t)(idx[dst] / N);         m->tc = (int8_t)(idx[dst] % N);
            }
            i += 2; moved = 1;
        } else {
            outv[out] = (int8_t)vals[i];
            move_t *m = &mv[n_mv++];
            m->v = (int8_t)vals[i]; m->merged = 0;
            m->fr = (int8_t)(idx[srci[i]] / N); m->fc = (int8_t)(idx[srci[i]] % N);
            m->tr = (int8_t)(idx[dst] / N);     m->tc = (int8_t)(idx[dst] % N);
            if (srci[i] != dst) moved = 1;
            i++;
        }
        out++;
    }
    for (int i = 0; i < N; i++) line[i] = outv[i];
    (void)dir_r; (void)dir_c;
    return moved;
}

static int do_move(int dr, int dc)
{
    n_mv = 0;
    int moved = 0;
    for (int l = 0; l < N; l++) {
        int8_t line[N];
        int idx[N];
        for (int i = 0; i < N; i++) {
            int r, c;
            if (dc) { r = l; c = dc > 0 ? N - 1 - i : i; }
            else    { c = l; r = dr > 0 ? N - 1 - i : i; }
            line[i] = bd[r][c];
            idx[i] = r * N + c;
        }
        moved |= slide_line(line, idx, dr, dc);
        for (int i = 0; i < N; i++) {
            int r = idx[i] / N, c = idx[i] % N;
            bd[r][c] = line[i];
        }
    }
    if (moved) { slide_t0 = now_ms(); }
    return moved;
}

static int any_move(void)
{
    for (int r = 0; r < N; r++)
        for (int c = 0; c < N; c++) {
            if (!bd[r][c]) return 1;
            if (c + 1 < N && bd[r][c] == bd[r][c + 1]) return 1;
            if (r + 1 < N && bd[r][c] == bd[r + 1][c]) return 1;
        }
    return 0;
}

static void num(const band *b, int x, int y, int v, int size)
{
    char s[8];
    int val = 1 << v;
    fx_fmt(s, sizeof s, "%d", val);
    const font_t *f = val >= 1000 ? &font_s : (size ? &font_m : &font_s);
    px c = v <= 2 ? DARK_TXT : LITE_TXT;
    gfx_text_c(b, x + TILE / 2, y + TILE / 2 + f->base / 2 - 1, f, c, s);
}

static void tile(const band *b, int x, int y, int v, int sz)
{
    int d = (TILE - sz) / 2;
    px col = tile_col[v < NCOL ? v : NCOL - 1];
    gfx_round(b, x + d, y + d, sz, sz, 4, col);
    if (v && sz == TILE) num(b, x, y, v, 1);
}

static void draw(const band *b, int anim, int k)
{
    char s[16];
    gfx_clear(b, PAGE_BG);
    gfx_round(b, BX, BY, N * TILE + (N + 1) * GAP, N * TILE + (N + 1) * GAP, 6, BOARD_BG);
    for (int r = 0; r < N; r++)
        for (int c = 0; c < N; c++)
            gfx_round(b, cellx(c), celly(r), TILE, TILE, 4, tile_col[0]);

    gfx_text(b, 14, 30, &font_l, RGB(119, 110, 101), "2048");
    gfx_round(b, 120, 8, 52, 28, 4, BOARD_BG);
    gfx_round(b, 178, 8, 52, 28, 4, BOARD_BG);
    gfx_text_c(b, 146, 19, &font_s, RGB(238, 228, 218), TR("SCORE", "СЧЁТ"));
    gfx_text_c(b, 204, 19, &font_s, RGB(238, 228, 218), TR("BEST", "РЕКОРД"));
    fx_fmt(s, sizeof s, "%d", score);   gfx_text_c(b, 146, 33, &font_s, WHITE, s);
    fx_fmt(s, sizeof s, "%d", best);    gfx_text_c(b, 204, 33, &font_s, WHITE, s);

    if (anim) {
        for (int i = 0; i < n_mv; i++) {
            move_t *m = &mv[i];
            int x = cellx(m->fc) + (cellx(m->tc) - cellx(m->fc)) * k / 256;
            int y = celly(m->fr) + (celly(m->tr) - celly(m->fr)) * k / 256;
            tile(b, x, y, m->v, TILE);
        }
    } else {
        for (int r = 0; r < N; r++)
            for (int c = 0; c < N; c++) {
                if (!bd[r][c]) continue;
                int sz = TILE;
                if (r == pop_r && c == pop_c) {
                    uint32_t dt = now_ms() - pop_t0;
                    if (dt < POP_MS) sz = TILE * (40 + 60 * (int)dt / POP_MS) / 100;
                }
                tile(b, cellx(c), celly(r), bd[r][c], sz);
            }
    }
    if (over) game_banner(b, TR("Game over", "Конец"), TR("▶ again", "▶ заново"), RGB(246, 94, 59));
    else if (won == 1) game_banner(b, "2048!", TR("▶ next", "▶ дальше"), RGB(237, 194, 46));
}

void run_2048(void)
{
    game_exit_button(BTN_UP);
    best = (int)hi_get(0);
    reset();
    uint32_t last = now_ms();
    int animating = 0;
    while (!game_quit()) {
        in_poll();
        if (animating) {
            uint32_t dt = now_ms() - slide_t0;
            if (dt >= SLIDE_MS) {
                animating = 0;
                spawn();
                if (!any_move()) { over = 1; SND(snd_die); hi_set(0, (uint32_t)score); best = (int)hi_get(0); }
            }
        } else if (over || won == 1) {
            if (in_hit() & B_RIGHT) { if (over) reset(); else won = 2; }
        } else {
            uint32_t h = in_hit();
            int moved = 0;
            if (h & B_UP)    moved = do_move(-1, 0);
            if (h & B_DOWN)  moved = do_move(1, 0);
            if (h & B_LEFT)  moved = do_move(0, -1);
            if (h & B_RIGHT) moved = do_move(0, 1);
            if (moved) { animating = 1; SND(snd_click); if (score > best) best = score; }
        }
        int k = animating ? (int)((now_ms() - slide_t0) * 256 / SLIDE_MS) : 0;
        if (k > 256) k = 256;
        fb_begin();
        for (band *b; (b = fb_next()); ) draw(b, animating, k);
        game_frame_wait(&last, 25);
    }
    hi_set(0, (uint32_t)score);
}
