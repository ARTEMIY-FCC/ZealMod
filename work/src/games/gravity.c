/* gravity.c — «Gravity Defied»: мотоцикл как две точки на жёсткой связи,
 * интегрирование по Верле.  Такая модель не разъезжается в целых числах и
 * даёт ту самую валкую езду по холмам. */
#include "game.h"
#include "snd.h"

#define STEP    8                  /* шаг рельефа, пикселей */
#define NPT     420                /* точек рельефа */
#define TRACK   (STEP * (NPT - 1))
#define WHEEL   7
#define ROD     26                 /* база мотоцикла */
#define GRAV    62                 /* Q8 за шаг */
#define DT      16

typedef struct { int x, y, px, py, on; } pt;

static int16_t *hgt, *far_;
static pt rear, front;
static int camx, camy, tries, done, crashed, best_pct;
static uint32_t t_start, t_end;

static int ground_at(int wx)       /* Q8 -> пиксели */
{
    int p = wx >> 8;
    if (p < 0) p = 0;
    if (p > TRACK - 1) p = TRACK - 1;
    int i = p / STEP, f = p % STEP;
    int a = hgt[i], b = hgt[i + 1 < NPT ? i + 1 : i];
    return a + (b - a) * f / STEP;
}

static void gen_track(void)
{
    int y = 170, v = 0;
    for (int i = 0; i < NPT; i++) {
        if (i < 6) { hgt[i] = (int16_t)y; continue; }
        int amp = 3 + (int)(plat_rand() % 5);
        v += (int)(plat_rand() % (2 * amp + 1)) - amp;
        v = iclamp(v, -5, 5);
        y += v;
        if (y < 90) { y = 90; v = 2; }
        if (y > 205) { y = 205; v = -2; }
        hgt[i] = (int16_t)y;
    }
    for (int k = 0; k < 4; k++)                       /* сгладить */
        for (int i = 1; i < NPT - 1; i++)
            hgt[i] = (int16_t)((hgt[i - 1] + 2 * hgt[i] + hgt[i + 1]) / 4);
    int fy = 120, fv = 0;                              /* дальняя гряда */
    for (int i = 0; i < NPT; i++) {
        fv += (int)(plat_rand() % 5) - 2;
        fv = iclamp(fv, -4, 4);
        fy = iclamp(fy + fv, 60, 150);
        far_[i] = (int16_t)fy;
    }
    for (int k = 0; k < 3; k++)
        for (int i = 1; i < NPT - 1; i++)
            far_[i] = (int16_t)((far_[i - 1] + 2 * far_[i] + far_[i + 1]) / 4);
}

static void reset_bike(void)
{
    rear.x = 40 << 8; rear.y = (ground_at(40 << 8) - WHEEL) << 8;
    front.x = (40 + ROD) << 8; front.y = (ground_at((40 + ROD) << 8) - WHEEL) << 8;
    rear.px = rear.x; rear.py = rear.y;
    front.px = front.x; front.py = front.y;
    crashed = 0; done = 0;
    t_start = now_ms();
}

#define VMAX (18 << 8)             /* пиксель за шаг: дальше физика врёт */

static void integrate(pt *p, int ax, int ay)
{
    int vx = iclamp(p->x - p->px, -VMAX, VMAX), vy = iclamp(p->y - p->py, -VMAX, VMAX);
    vx = vx * 252 / 256; vy = vy * 254 / 256;         /* трение о воздух */
    p->px = p->x; p->py = p->y;
    p->x += vx + ax;
    p->y += vy + ay;
}

static void constrain(void)
{
    for (int k = 0; k < 4; k++) {
        int dx = front.x - rear.x, dy = front.y - rear.y;
        int d = fx_hypot(dx >> 4, dy >> 4) << 4;
        if (d < 16) d = 16;
        int diff = ((ROD << 8) - d) / 2;
        if (d < 256) d = 256;
        int nx = (dx << 8) / d, ny = (dy << 8) / d;   /* единичный вектор Q8 */
        front.x += nx * diff >> 8; front.y += ny * diff >> 8;
        rear.x -= nx * diff >> 8;  rear.y -= ny * diff >> 8;
    }
}

static void collide(pt *p, int drive, int brake)
{
    int g = ground_at(p->x) - WHEEL;
    p->on = 0;
    if ((p->y >> 8) < g) return;
    p->on = 1;
    /* нормаль поверхности из наклона */
    p->x = iclamp(p->x, 0, TRACK << 8);
    int g1 = ground_at(p->x - (6 << 8)), g2 = ground_at(p->x + (6 << 8));
    int tx = 12, ty = g2 - g1;
    int tl = fx_hypot(tx, ty);
    if (!tl) tl = 1;
    p->y = g << 8;
    int vx = p->x - p->px, vy = p->y - p->py;
    /* убрать скорость вдоль нормали, оставить вдоль касательной */
    int vt = (vx * tx + vy * ty) / tl;
    vx = vt * tx / tl; vy = vt * ty / tl;
    if (drive) { vx += drive * tx / tl; vy += drive * ty / tl; }
    if (brake) { vx = vx * 3 / 4; vy = vy * 3 / 4; }
    p->px = p->x - vx;
    p->py = p->y - vy;
}

static void lean(int dir)
{
    int cx = (rear.x + front.x) / 2, cy = (rear.y + front.y) / 2;
    int t = dir * 26;
    int dx = front.x - cx, dy = front.y - cy;
    front.px += -dy * t >> 8; front.py += dx * t >> 8;
    dx = rear.x - cx; dy = rear.y - cy;
    rear.px += -dy * t >> 8; rear.py += dx * t >> 8;
}

static void physics(uint32_t held)
{
    int drive = 0, brake = 0;
    if (held & B_UP) drive = 62;
    if (held & B_DOWN) brake = 1;
    if (held & B_LEFT) lean(-1);
    if (held & B_RIGHT) lean(1);
    integrate(&rear, 0, GRAV);
    integrate(&front, 0, GRAV);
    constrain();
    collide(&rear, drive, brake);
    collide(&front, 0, brake);
    constrain();

    /* голова седока: перпендикуляр к раме, 22 пикселя «вверх» */
    int mx = (rear.x + front.x) / 2, my = (rear.y + front.y) / 2;
    int dx = (front.x - rear.x) >> 8, dy = (front.y - rear.y) >> 8;
    int d = fx_hypot(dx, dy);
    if (d < 1) d = 1;
    int hx = mx + (dy * 20 << 8) / d, hy = my - (dx * 20 << 8) / d;   /* перпендикуляр «вверх» */
    if ((hy >> 8) > ground_at(hx) + 3) { if (!crashed) SND(snd_die); crashed = 1; }
    if (dx < iabs(dy) / 3 && (rear.on || front.on)) crashed = 1;      /* завалился набок */
    if ((rear.x >> 8) > TRACK - 60) { if (!done) SND(snd_win); done = 1; t_end = now_ms(); }
}

static void draw(const band *b)
{
    char s[64];
    gfx_vgrad(b, 0, 0, SCR_W, SCR_H, RGB(96, 170, 250), RGB(206, 232, 255));
    for (int x = 0; x < SCR_W; x++) {                 /* дальние холмы */
        int wx = (camx >> 8) / 2 + x;
        int i = iclamp(wx / STEP, 0, NPT - 2);
        int f = wx % STEP, ya = far_[i], yb = far_[i + 1];
        int gy = ya + (yb - ya) * f / STEP - (camy >> 9);
        int y0 = imax(gy, b->y0);
        if (y0 < b->y1) gfx_fill(b, x, y0, 1, b->y1 - y0, RGB(108, 134, 112));
        gfx_fill(b, x, gy, 1, 3, RGB(126, 152, 128));
    }
    for (int x = 0; x < SCR_W; x++) {                 /* земля */
        int wx = (camx >> 8) + x;
        int gy = ground_at(wx << 8) - (camy >> 8);
        int y0 = imax(gy, b->y0);
        if (y0 >= b->y1) continue;
        gfx_fill(b, x, y0, 1, b->y1 - y0, RGB(86, 62, 42));
        gfx_fill(b, x, gy, 1, 4, RGB(74, 150, 56));                 /* трава */
        gfx_fill(b, x, gy + 4, 1, 2, RGB(52, 106, 40));
        if (((wx >> 4) + (gy >> 3)) & 1)                            /* прожилки грунта */
            gfx_fill(b, x, gy + 10, 1, 3, RGB(72, 52, 34));
        gfx_fill(b, x, gy + 26, 1, 2, RGB(74, 54, 36));
    }
    /* финиш */
    int fx_x = TRACK - 40 - (camx >> 8);
    if (fx_x > -20 && fx_x < SCR_W + 20) {
        int gy = ground_at((TRACK - 40) << 8) - (camy >> 8);
        for (int i = 0; i < 8; i++)
            for (int j = 0; j < 2; j++)
                gfx_fill(b, fx_x + j * 8, gy - 56 + i * 7, 8, 7, ((i + j) & 1) ? WHITE : RGB(20, 20, 20));
        gfx_fill(b, fx_x - 2, gy - 56, 2, 56, RGB(90, 90, 100));
    }

    int rx = (rear.x >> 8) - (camx >> 8), ry = (rear.y >> 8) - (camy >> 8);
    int fx2 = (front.x >> 8) - (camx >> 8), fy = (front.y >> 8) - (camy >> 8);
    int mx = (rx + fx2) / 2, my = (ry + fy) / 2;
    int dx = fx2 - rx, dy = fy - ry, d = fx_hypot(dx, dy);
    if (!d) d = 1;
    int hx = mx + dy * 20 / d, hy = my - dx * 20 / d;      /* «вверх» рамы */
    gfx_line(b, rx, ry, hx, hy, RGB(180, 30, 30));
    gfx_line(b, fx2, fy, hx, hy, RGB(180, 30, 30));
    gfx_line(b, rx, ry, fx2, fy, RGB(60, 60, 70));
    gfx_disc(b, hx - dx * 4 / d, hy - dy * 4 / d, 5, RGB(230, 120, 40));   /* седок */
    gfx_line(b, hx, hy, fx2, fy - 4, RGB(230, 120, 40));
    for (int w = 0; w < 2; w++) {
        int wx = w ? fx2 : rx, wy = w ? fy : ry;
        gfx_ring(b, wx, wy, WHEEL, 2, RGB(24, 24, 28));
        gfx_disc(b, wx, wy, 2, RGB(150, 150, 160));
    }

    gfx_fill(b, 0, 0, SCR_W, 22, px_scale(RGB(0, 0, 0), 110));
    int pct = iclamp((rear.x >> 8) * 100 / TRACK, 0, 100);
    uint32_t sec = ((done ? t_end : now_ms()) - t_start) / 1000;
    fx_fmt(s, sizeof s, TR("%d%%   %d:%02d   tries %d", "%d%%   %d:%02d   попыток %d"), pct, (int)sec / 60, (int)sec % 60, tries);
    gfx_text(b, 6, 16, &font_s, WHITE, s);
    gfx_fill(b, 0, 20, SCR_W * pct / 100, 2, RGB(90, 230, 120));
    if (crashed) game_banner(b, TR("Crashed", "Упал"), TR("▶ again", "▶ ещё раз"), RGB(230, 70, 50));
    if (done) {
        fx_fmt(s, sizeof s, TR("in %d:%02d, tries %d", "за %d:%02d, попыток %d"), (int)sec / 60, (int)sec % 60, tries);
        game_banner(b, TR("Finish!", "Финиш!"), s, RGB(90, 230, 120));
    }
}

void run_gravity(void)
{
    game_exit_button(BTN_DOWN);
    mem_reset();
    hgt = mem_alloc(NPT * 2);
    far_ = mem_alloc(NPT * 2);
    if (!hgt || !far_) return;
    gen_track();
    tries = 1; best_pct = 0;
    reset_bike();
    uint32_t last = now_ms();
    while (!game_quit()) {
        in_poll();
        if (crashed || done) {
            if (in_hit() & B_RIGHT) {
                if (done) { gen_track(); tries = 0; }
                tries++;
                reset_bike();
            }
        } else {
            physics(in_held());
            int px_ = (rear.x >> 8) - 70;
            camx += (((px_ < 0 ? 0 : px_) << 8) - camx) / 6;
            int py = ground_at(rear.x) - 150;
            camy += ((py << 8) - camy) / 12;
            int pct = iclamp((rear.x >> 8) * 100 / TRACK, 0, 100);
            if (pct > best_pct) { best_pct = pct; hi_set(0, (uint32_t)pct); }
        }
        fb_begin();
        for (band *b; (b = fb_next()); ) draw(b);
        game_frame_wait(&last, DT);
    }
}
