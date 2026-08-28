/* maze3d.c — лабиринт от первого лица.  Классический DDA по сетке: на кадр
 * считаются 240 столбцов (высота, текстура, столбец текстуры, затенение),
 * а полосы кадра потом просто выбирают из этой таблицы. */
#include "game.h"
#include "snd.h"
#include "mazegen.h"

#define TEX   32          /* больше не влезает: вся крупная память под кадр */
#define FOV   (FX_TURN * 62 / 360)
#define WALLH 195
#define MAXD  40

typedef struct { int16_t h; uint8_t tex, col; uint8_t shade; } colinfo;

static maze_t mz;
static colinfo *cols;
static uint8_t *tex_wall, *tex_exit;
static px *pal_wall, *pal_exit;
static int posx, posy, ang, level;
static uint32_t t_start, t_done;

static void make_textures(void)
{
    tex_wall = mem_alloc(TEX * TEX);
    tex_exit = mem_alloc(TEX * TEX);
    pal_wall = mem_alloc(64 * sizeof(px));
    pal_exit = mem_alloc(64 * sizeof(px));
    for (int i = 0; i < 64; i++) {                    /* холодный камень */
        int k = i * 4;
        pal_wall[i] = px_pack(iclamp(38 + k * 3 / 4, 0, 255), iclamp(46 + k * 4 / 5, 0, 255),
                              iclamp(70 + k, 0, 255));
        pal_exit[i] = px_pack(iclamp(k / 3, 0, 255), iclamp(60 + k, 0, 255), iclamp(50 + k * 3 / 4, 0, 255));
    }
    for (int y = 0; y < TEX; y++)
        for (int x = 0; x < TEX; x++) {
            int row = y / 8, off = (row & 1) ? 8 : 0;     /* кладка 16x8 */
            int bx = (x + off) % 16, by = y % 8;
            int mortar = (bx < 2 || by < 2);
            int brick = ((x + off) / 16 * 7 + row * 13) & 7;
            int n = (int)(plat_rand() % 5) - 2;
            int v = mortar ? 14 : 30 + brick + n;
            if (!mortar && (bx < 3 || by < 3)) v += 8;    /* фаска */
            if (!mortar && (bx > 13 || by > 6)) v -= 6;
            tex_wall[y * TEX + x] = (uint8_t)iclamp(v, 0, 63);
            int r = iabs(x - TEX / 2) + iabs(y - TEX / 2);
            tex_exit[y * TEX + x] = (uint8_t)iclamp(60 - r + ((x ^ y) & 3), 0, 63);
        }
}

static int build(void)
{
    static const uint8_t sizes[] = { 4, 5, 6, 7, 8, 9, 10 };
    mem_reset();
    cols = mem_alloc(SCR_W * sizeof(colinfo));
    make_textures();
    if (!cols || !tex_wall || !tex_exit || !pal_wall || !pal_exit) return 0;
    int n = sizes[level < (int)sizeof sizes ? level : (int)sizeof sizes - 1];
    if (!maze_make(&mz, n, n)) return 0;
    posx = (1 << 16) + 32768; posy = (1 << 16) + 32768; ang = 0;
    t_start = now_ms(); t_done = 0;
    return 1;
}

static int solid(int x, int y) { return maze_at(&mz, x, y); }

static void cast(void)
{
    for (int x = 0; x < SCR_W; x++) {
        int da = -FOV / 2 + FOV * x / SCR_W;
        int a = ang + da;
        int dx = fx_cos(a), dy = fx_sin(a);          /* Q14 */
        int adx = iabs(dx), ady = iabs(dy);
        int ddx = adx > 4 ? (1 << 30) / adx : (1 << 24);   /* Q16 на клетку, влезает в 32 бита */
        int ddy = ady > 4 ? (1 << 30) / ady : (1 << 24);
        int mx = posx >> 16, my = posy >> 16;
        int stepx = dx > 0 ? 1 : -1, stepy = dy > 0 ? 1 : -1;
        int fx_ = dx > 0 ? 65536 - (posx & 0xFFFF) : (posx & 0xFFFF);
        int fy_ = dy > 0 ? 65536 - (posy & 0xFFFF) : (posy & 0xFFFF);
        int sdx = (int)((int64_t)fx_ * ddx >> 16);
        int sdy = (int)((int64_t)fy_ * ddy >> 16);
        int side = 0, steps = 0;
        while (steps++ < MAXD) {
            if (sdx < sdy) { sdx += ddx; mx += stepx; side = 0; }
            else           { sdy += ddy; my += stepy; side = 1; }
            if (solid(mx, my)) break;
        }
        int dist = side ? sdy - ddy : sdx - ddx;      /* Q16, вдоль луча */
        int cosd = fx_cos(da);
        dist = (int)((int64_t)dist * cosd >> 14);     /* убираем «рыбий глаз» */
        if (dist < 4096) dist = 4096;
        int h = (WALLH << 16) / dist;
        if (h > 2000) h = 2000;
        int hit;
        if (side == 0) hit = posy + (int)((int64_t)dist * dy >> 14);
        else           hit = posx + (int)((int64_t)dist * dx >> 14);
        int tcol = (hit & 0xFFFF) * TEX >> 16;
        if ((side == 0 && dx < 0) || (side == 1 && dy > 0)) tcol = TEX - 1 - tcol;
        int shade = 256 - imin(dist >> 10, 190);
        if (side) shade = shade * 3 / 4;
        cols[x].h = (int16_t)h;
        cols[x].tex = (uint8_t)(mx == mz.ex && my == mz.ey ? 1 : 0);
        cols[x].col = (uint8_t)tcol;
        cols[x].shade = (uint8_t)iclamp(shade, 12, 255);
    }
}

static void draw(const band *b)
{
    char s[48];
    gfx_vgrad(b, 0, 0, SCR_W, 120, RGB(24, 26, 44), RGB(8, 9, 16));
    gfx_vgrad(b, 0, 120, SCR_W, 120, RGB(10, 9, 8), RGB(46, 38, 30));
    for (int x = 0; x < SCR_W; x++) {
        colinfo *c = &cols[x];
        int top = 120 - c->h / 2, bot = 120 + c->h / 2;
        int y0 = imax(top, b->y0), y1 = imin(bot, b->y1);
        if (y0 >= y1) continue;
        const uint8_t *t = (c->tex ? tex_exit : tex_wall) + c->col;
        const px *pal = c->tex ? pal_exit : pal_wall;
        int step = (TEX << 16) / (c->h ? c->h : 1);
        px *dst;
        for (int y = y0; y < y1; y++) {
            int v = ((y - top) * step >> 16) & (TEX - 1);
            dst = band_row(b, y) + x;
            *dst = px_scale(pal[t[v * TEX]], c->shade);
        }
    }
    /* карта в углу */
    int ms = 3, mw = mz.gw * ms;
    gfx_fill(b, SCR_W - mw - 6, 4, mw + 4, mz.gh * ms + 4, RGB(0, 0, 0));
    for (int y = 0; y < mz.gh; y++)
        for (int x = 0; x < mz.gw; x++)
            if (maze_at(&mz, x, y))
                gfx_fill(b, SCR_W - mw - 4 + x * ms, 6 + y * ms, ms, ms, RGB(70, 90, 180));
    gfx_fill(b, SCR_W - mw - 4 + mz.ex * ms, 6 + mz.ey * ms, ms, ms, RGB(70, 240, 130));
    gfx_fill(b, SCR_W - mw - 4 + (posx >> 16) * ms, 6 + (posy >> 16) * ms, ms, ms, RGB(255, 214, 70));

    uint32_t sec = ((t_done ? t_done : now_ms()) - t_start) / 1000;
    fx_fmt(s, sizeof s, TR("lv %d   %d:%02d", "ур %d   %d:%02d"), level + 1, (int)sec / 60, (int)sec % 60);
    gfx_text(b, 8, 20, &font_m, RGB(220, 225, 240), s);
    if (t_done) game_banner(b, TR("Way out!", "Выход найден!"), TR("▶ next", "▶ дальше"),
                              RGB(70, 240, 130));
}

void run_maze3d(void)
{
    game_exit_button(BTN_DOWN);
    level = 0;
    if (!build()) {                     /* памяти не хватило — честно скажем */
        uint32_t t = now_ms();
        while (!game_quit()) {
            in_poll();
            fb_begin();
            for (band *b; (b = fb_next()); ) {
                gfx_clear(b, RGB(20, 10, 10));
                gfx_text_c(b, 120, 120, &font_m, WHITE, TR("out of memory", "не хватило памяти"));
            }
            game_frame_wait(&t, 33);
        }
        return;
    }
    uint32_t last = now_ms();
    while (!game_quit()) {
        in_poll();
        if (t_done) {
            if (in_hit() & B_RIGHT) { level++; if (!build()) return; }
        } else {
            uint32_t h = in_held();
            if (h & B_LEFT) ang -= 12;
            if (h & B_RIGHT) ang += 12;
            int mv = (h & B_UP) ? 3600 : (h & B_DOWN) ? -2400 : 0;
            if (mv) {
                int nx = posx + (fx_cos(ang) * mv >> 14);
                int ny = posy + (fx_sin(ang) * mv >> 14);
                int r = 13000;
                if (!solid((nx + (mv > 0 ? r : -r)) >> 16, posy >> 16)) posx = nx;
                if (!solid(posx >> 16, (ny + (mv > 0 ? r : -r)) >> 16)) posy = ny;
                if ((posx >> 16) == mz.ex && (posy >> 16) == mz.ey && !t_done) {
                    SND(snd_win);
                    t_done = now_ms();
                    hi_set(0, (uint32_t)(level + 1));
                }
            }
        }
        cast();
        fb_begin();
        for (band *b; (b = fb_next()); ) draw(b);
        game_frame_wait(&last, 33);
    }
}
