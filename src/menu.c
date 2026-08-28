/* menu.c — экран выбора модуля.  Три раскладки на выбор темы:
 *   0 — обложки в перспективе (cover flow): каждая рисуется столбцами, по
 *       столбцу считается перспективно-верная координата текстуры;
 *   1 — сетка 3x3 с прокруткой;
 *   2 — список с мелкими обложками.
 * Цвета и раскладку задаёт тема (zm_theme), список модулей — таблица ZealMod.
 */
#include "plat.h"
#include "fx.h"
#include "game.h"
#include "nvram.h"
#include "snd.h"
#include "zmtab.h"

#define CX        120
#define HORIZON   100            /* уровень глаз = середина обложки */
#define FOV       300
#define Z0        288
#define ZSTEP     90
#define ZTAIL     25
#define SIDE_STEP 45
#define HW        48            /* полуширина обложки в мировых единицах */
#define MAXANG    (FX_TURN * 60 / 360)

typedef struct {
    int sxl, sxr;               /* края на экране */
    int ytl, ybl, ytr, ybr;     /* верх/низ левого и правого края */
    int wl, wr;                 /* 1/z для перспективной интерполяции */
    int dim;
    zm_item_t it;
} quad;

static const zm_theme_t *th;

static void geom(quad *q, const zm_item_t *it, int d16)
{
    int ad = iabs(d16), c1 = imin(ad, 65536);
    int sgn = d16 < 0 ? -1 : 1;
    int spacing = th->spacing ? th->spacing : 92;
    int ang = -sgn * (int)((int64_t)MAXANG * c1 >> 16);
    int X = sgn * ((int)((int64_t)spacing * c1 >> 16) + (ad > 65536 ? (int)((int64_t)SIDE_STEP * (ad - 65536) >> 16) : 0));
    int Z = Z0 + (int)((int64_t)ZSTEP * c1 >> 16) + (ad > 65536 ? (int)((int64_t)ZTAIL * (ad - 65536) >> 16) : 0);
    int ca = fx_cos(ang), sa = fx_sin(ang);
    int xl = X - (HW * ca >> 14), zl = Z + (HW * sa >> 14);
    int xr = X + (HW * ca >> 14), zr = Z - (HW * sa >> 14);
    if (zl < 60) zl = 60;
    if (zr < 60) zr = 60;
    q->sxl = CX + xl * FOV / zl;
    q->sxr = CX + xr * FOV / zr;
    int hl = HW * FOV / zl, hr = HW * FOV / zr;
    q->ytl = HORIZON - hl; q->ybl = HORIZON + hl;
    q->ytr = HORIZON - hr; q->ybr = HORIZON + hr;
    q->wl = 65536 / zl; q->wr = 65536 / zr;
    q->dim = 256 - imin(iabs(d16) >> 10, 70);
    q->it = *it;
}

static void column(const band *b, int x, int ytop, int ybot, const uint8_t *src,
                   const px *pal, int ucol, int dim)
{
    int h = ybot - ytop;
    if (h <= 0 || (unsigned)x >= SCR_W) return;
    int step = (COVER_H << 16) / h;
    int y0 = imax(ytop, b->y0), y1 = imin(ybot, b->y1);
    const uint8_t *col = src + ucol;
    for (int y = y0; y < y1; y++) {
        int v = (y - ytop) * step >> 16;
        px c = pal[col[v * COVER_W]];
        band_row(b, y)[x] = dim >= 256 ? c : px_scale(c, dim);
    }
    /* отражение: те же столбцы, снизу вверх, гаснут */
    int rh = h * th->reflection / 100;
    if (rh <= 0) return;
    int r0 = imax(ybot, b->y0), r1 = imin(ybot + rh, b->y1);
    for (int y = r0; y < r1; y++) {
        int k = y - ybot;
        int v = (rh - k) * COVER_H / rh;
        if (v >= COVER_H) v = COVER_H - 1;
        int f = (rh - k) * 150 / rh;
        px c = pal[col[v * COVER_W]];
        band_row(b, y)[x] = px_scale(c, f * dim >> 8);
    }
}

static void draw_quad(const band *b, const quad *q)
{
    int w = q->sxr - q->sxl;
    if (w <= 0 || !q->it.cover) return;
    const uint8_t *src = q->it.cover;
    const px *pal = q->it.pal;
    int x0 = imax(q->sxl, 0), x1 = imin(q->sxr, SCR_W);
    for (int x = x0; x < x1; x++) {
        int t = ((x - q->sxl) << 8) / w;
        int aa = (256 - t) * q->wl, bb = t * q->wr;
        int u = (bb << 8) / (aa + bb);                 /* 0..256, перспективно верно */
        int ucol = u * (COVER_W - 1) >> 8;
        int ytop = q->ytl + ((q->ytr - q->ytl) * t >> 8);
        int ybot = q->ybl + ((q->ybr - q->ybl) * t >> 8);
        column(b, x, ytop, ybot, src, pal, ucol, q->dim);
    }
}

static void background(const band *b)
{
    if ((th->flags & 1) && th->wallpaper) {
        gfx_blit(b, 0, 0, SCR_W, SCR_H, (const px *)(uintptr_t)th->wallpaper);
        return;
    }
    gfx_vgrad(b, 0, 0, SCR_W, HORIZON + 50, th->bg_top, th->bg_bot);
    gfx_vgrad(b, 0, HORIZON + 50, SCR_W, SCR_H - HORIZON - 50, th->fl_top, th->fl_bot);
    gfx_fill(b, 0, HORIZON + 50, SCR_W, 1, th->line);
}

/* --- раскладка «сетка» и «список» ---------------------------------------- */
#define GRID_C  3
#define GRID_S  66
#define GRID_G  8
#define LIST_H  46

static void draw_tile(const band *b, int x, int y, int s, const zm_item_t *it, int sel)
{
    if (sel) gfx_round(b, x - 3, y - 3, s + 6, s + 6, 8, th->accent);
    if (it->cover)
        gfx_blit8_scaled(b, x, y, s, s, it->cover, COVER_W, COVER_H, it->pal, -1,
                         sel ? 256 : 190);
    else
        gfx_round(b, x, y, s, s, 6, th->text_dim);
}

static void draw_grid(const band *b, int n, int sel, int scroll)
{
    background(b);
    for (int i = 0; i < n; i++) {
        int r = i / GRID_C, c = i % GRID_C;
        int x = 12 + c * (GRID_S + GRID_G);
        int y = 14 + r * (GRID_S + GRID_G) - scroll;
        if (y > SCR_H || y + GRID_S < 0) continue;
        zm_item_t it;
        zm_item(i, &it);
        draw_tile(b, x, y, GRID_S, &it, i == sel);
    }
    gfx_dim(b, 0, SCR_H - 26, SCR_W, 26, 40);
    zm_item_t it;
    zm_item(sel, &it);
    gfx_text_c(b, CX, SCR_H - 6, &font_m, th->text, it.title);
}

static void draw_list(const band *b, int n, int sel, int scroll)
{
    background(b);
    for (int i = 0; i < n; i++) {
        int y = 8 + i * LIST_H - scroll;
        if (y > SCR_H || y + LIST_H < 0) continue;
        zm_item_t it;
        zm_item(i, &it);
        if (i == sel) gfx_round(b, 6, y, SCR_W - 12, LIST_H - 6, 8, th->accent);
        if (it.cover)
            gfx_blit8_scaled(b, 12, y + 4, 32, 32, it.cover, COVER_W, COVER_H,
                             it.pal, -1, 256);
        gfx_text(b, 54, y + 27, &font_m, i == sel ? th->bg_bot : th->text, it.title);
    }
}

/* --- сам экран ----------------------------------------------------------- */
#ifndef PLAT_HOST
extern volatile int zg_cmd_app, zg_cmd_exit, zg_cmd_back;   /* app: 0 = ничего, иначе индекс+1 */
#else
static int zg_cmd_app, zg_cmd_exit, zg_cmd_back;
#endif

void zm_splash(void);

static void launch(int sel)
{
    zm_item_t it;
    zm_item(sel, &it);
    if (!it.run) return;
    SND(snd_pick);
    zg_cmd_back = 0;
    game_exit_defaults();
    game_exit_button(it.exit_btn);
    game_exit_hold(it.exit_hold * 100);
    zm_set_slot(sel);
    mem_reset();
    it.run();
    mem_reset();
    zm_set_slot(0);
}

static void empty_screen(void)
{
    uint32_t last = now_ms();
    for (;;) {
        in_poll();
        if (in_hit() & (B_DOWN | B_UP)) plat_exit_to_stock();
        fb_begin();
        for (band *b; (b = fb_next()); ) {
            background(b);
            gfx_text_c(b, CX, 110, &font_m, th->text, TR("No programs", "Модулей нет"));
            gfx_text_c(b, CX, 140, &font_s, th->text_dim,
                       TR("build an image in ZealMod Studio",
                          "соберите образ в ZealMod Studio"));
        }
        game_frame_wait(&last, 33);
    }
}

void menu_run(void)
{
    th = zm_theme();
    int n = zm_count();
    zm_splash();
    if (n <= 0) empty_screen();

    int sel = 0, pos = 0, scroll = 0;
    uint32_t last = now_ms();
    for (;;) {
        in_poll();
        if (zg_cmd_app) {                      /* запуск с компьютера */
            int k = zg_cmd_app - 1;
            zg_cmd_app = 0;
            if (k < n) {
                sel = k; pos = sel << 16;
                launch(sel);
            }
        }
        if (zg_cmd_exit) { zg_cmd_exit = 0; plat_exit_to_stock(); }
        /* Управление одинаковое во всех раскладках — иначе запуск в сетке
         * и списке приходился бы на кнопку прокрутки:
         *   ◀ ▶ — соседний пункт, ▲ — запустить, ▼ — выйти в таймер. */
        uint32_t hit = in_rep();
        int was = sel, layout = th->layout;
        if (hit & B_RIGHT) sel = imin(sel + 1, n - 1);
        if (hit & B_LEFT)  sel = imax(sel - 1, 0);
        if (sel != was) SND(snd_click);
        int settled = layout || iabs((sel << 16) - pos) < 40000;
        if ((in_hit() & B_UP) && settled) launch(sel);
        if (in_hit() & B_DOWN) plat_exit_to_stock();

        int target = sel << 16;
        pos += (target - pos) * 5 / 16;
        if (iabs(target - pos) < 256) pos = target;

        if (layout == 1) {
            int row = sel / GRID_C, want = imax(0, row * (GRID_S + GRID_G) - 70);
            scroll += (want - scroll + 1) / 2;
        } else if (layout == 2) {
            int want = imax(0, sel * LIST_H - 90);
            scroll += (want - scroll + 1) / 2;
        }

        if (layout) {
            fb_begin();
            for (band *b; (b = fb_next()); )
                (layout == 1 ? draw_grid : draw_list)(b, n, sel, scroll);
        } else {
            /* дальние рисуем первыми, центральную — последней */
            quad qs[7];
            int cnt = 0;
            int c = pos >> 16;
            for (int i = c - 3; i <= c + 3; i++) {
                if (i < 0 || i >= n) continue;
                zm_item_t it;
                zm_item(i, &it);
                geom(&qs[cnt++], &it, (i << 16) - pos);
            }
            for (int i = 0; i < cnt; i++)                   /* сортировка «дальше — раньше» */
                for (int j = i + 1; j < cnt; j++) {
                    int di = iabs((int)(qs[i].wl + qs[i].wr)), dj = iabs((int)(qs[j].wl + qs[j].wr));
                    if (dj < di) { quad t = qs[i]; qs[i] = qs[j]; qs[j] = t; }
                }
            int shown = iabs(target - pos) < 12000;
            zm_item_t cur;
            zm_item(sel, &cur);
            int tw = gfx_text_w(&font_l, cur.title);

            fb_begin();
            for (band *b; (b = fb_next()); ) {
                background(b);
                for (int i = 0; i < cnt; i++) draw_quad(b, &qs[i]);
                if (shown && !(th->flags & 2)) {
                    gfx_text_shadow(b, CX - tw / 2, 222, &font_l, th->text, th->shadow, cur.title);
                    if (!(th->flags & 4))
                        for (int i = 0; i < n && i < 20; i++) {
                            int x = CX - imin(n, 20) * 5 + i * 10;
                            gfx_fill(b, x, 232, 5, 3, i == sel ? th->accent : th->text_dim);
                        }
                }
            }
        }
        uint32_t now = now_ms();
        plat_sleep_ms(now - last < 16 ? 16 - (now - last) : 1);
        last = now_ms();
    }
}
