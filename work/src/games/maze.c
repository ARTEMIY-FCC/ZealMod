/* maze.c — лабиринт сверху: плавный ход, свет вокруг игрока, уровни растут. */
#include "game.h"
#include "snd.h"
#include "mazegen.h"

static maze_t mz;
static int level, cell, ox, oy;
static int px_, py_;             /* положение игрока, 16.16, в клетках сетки */
static int tx, ty, moving, dir;
static uint32_t t_start, t_done;

static const px WALL_A = RGB(64, 92, 220), WALL_B = RGB(28, 44, 130);
static const px FLOOR_ = RGB(14, 16, 34);

static void build(void)
{
    static const uint8_t sizes[] = { 5, 6, 7, 8, 9, 10, 11 };
    int n = sizes[level < (int)sizeof sizes ? level : (int)sizeof sizes - 1];
    mem_reset();
    maze_make(&mz, n, n);
    cell = 232 / mz.gw;
    if (cell < 6) cell = 6;
    ox = (SCR_W - cell * mz.gw) / 2;
    oy = 34 + (SCR_H - 34 - cell * mz.gh) / 2;
    px_ = 1 << 16; py_ = 1 << 16; moving = 0; dir = 3;
    t_start = now_ms(); t_done = 0;
}

static void try_move(int dx, int dy)
{
    int cx = px_ >> 16, cy = py_ >> 16;
    if (maze_at(&mz, cx + dx, cy + dy)) return;
    tx = cx + dx; ty = cy + dy; moving = 1;
    dir = dx > 0 ? 3 : dx < 0 ? 2 : dy > 0 ? 1 : 0;
}

static void draw(const band *b)
{
    char s[48];
    gfx_clear(b, RGB(6, 6, 14));
    int cx = px_ >> 16, cy = py_ >> 16;
    for (int y = 0; y < mz.gh; y++) {
        int sy = oy + y * cell;
        if (sy + cell < b->y0 || sy > b->y1) continue;
        for (int x = 0; x < mz.gw; x++) {
            int d = iabs(x - cx) + iabs(y - cy);
            int lit = 256 - imin(d * 10, 120);
            int sx = ox + x * cell;
            if (maze_at(&mz, x, y))
                gfx_fill(b, sx, sy, cell, cell, px_scale(px_mix(WALL_A, WALL_B, (x * 37 + y * 53) & 255), lit));
            else
                gfx_fill(b, sx, sy, cell, cell, px_scale(FLOOR_, lit + 40));
        }
    }
    /* выход */
    int ex = ox + mz.ex * cell, ey = oy + mz.ey * cell;
    int pulse = 180 + (int)((now_ms() / 4) % 150);
    gfx_fill(b, ex + 1, ey + 1, cell - 2, cell - 2, px_scale(RGB(70, 240, 130), pulse > 255 ? 511 - pulse : pulse));

    int sx = ox + (px_ * cell >> 16), sy = oy + (py_ * cell >> 16);
    gfx_disc(b, sx + cell / 2, sy + cell / 2, cell / 2 - 1, RGB(255, 214, 70));
    static const int8_t ed[4][2] = { { 0, -2 }, { 0, 2 }, { -2, 0 }, { 2, 0 } };
    gfx_disc(b, sx + cell / 2 + ed[dir][0], sy + cell / 2 + ed[dir][1], 1, RGB(60, 40, 0));

    gfx_fill(b, 0, 0, SCR_W, 30, RGB(10, 10, 22));
    fx_fmt(s, sizeof s, TR("level %d", "уровень %d"), level + 1);
    gfx_text(b, 8, 20, &font_m, RGB(120, 150, 255), s);
    uint32_t sec = ((t_done ? t_done : now_ms()) - t_start) / 1000;
    fx_fmt(s, sizeof s, "%d:%02d", (int)sec / 60, (int)sec % 60);
    gfx_text(b, 190, 20, &font_m, WHITE, s);
    if (t_done) game_banner(b, TR("Way out!", "Выход найден!"), TR("▶ next", "▶ дальше"),
                              RGB(70, 240, 130));
}

void run_maze(void)
{
    game_exit_button(BTN_DOWN);
    level = 0;
    build();
    uint32_t last = now_ms();
    while (!game_quit()) {
        in_poll();
        if (t_done) {
            if (in_hit() & B_RIGHT) { level++; build(); }
        } else if (!moving) {
            uint32_t h = in_held();
            if (h & B_UP) try_move(0, -1);
            else if (h & B_DOWN) try_move(0, 1);
            else if (h & B_LEFT) try_move(-1, 0);
            else if (h & B_RIGHT) try_move(1, 0);
        } else {
            int step = 12000;
            int dx = (tx << 16) - px_, dy = (ty << 16) - py_;
            px_ += iclamp(dx, -step, step);
            py_ += iclamp(dy, -step, step);
            if (px_ == (tx << 16) && py_ == (ty << 16)) {
                moving = 0;
                if (tx == mz.ex && ty == mz.ey) {
                    SND(snd_win);
                    t_done = now_ms();
                    hi_set(0, (uint32_t)(level + 1));
                }
            }
        }
        fb_begin();
        for (band *b; (b = fb_next()); ) draw(b);
        game_frame_wait(&last, 33);
    }
}
