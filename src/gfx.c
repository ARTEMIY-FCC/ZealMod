/* gfx.c — рисование в полосу кадра.  Всё режется по границам полосы и экрана,
 * так что игре не нужно знать, какая часть кадра сейчас в буфере. */
#include "plat.h"

px px_scale(px c, int q)
{
    int r, g, b;
    px_unpack(c, &r, &g, &b);
    return RGB(r * q >> 8, g * q >> 8, b * q >> 8);
}

px px_mix(px a, px bb, int t)
{
    int r1, g1, b1, r2, g2, b2;
    px_unpack(a, &r1, &g1, &b1);
    px_unpack(bb, &r2, &g2, &b2);
    return RGB(r1 + ((r2 - r1) * t >> 8), g1 + ((g2 - g1) * t >> 8), b1 + ((b2 - b1) * t >> 8));
}

static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

void gfx_clear(const band *b, px c)
{
    px *p = b->p;
    int n = (b->y1 - b->y0) * SCR_W;
    while (n--) *p++ = c;
}

void gfx_fill(const band *b, int x, int y, int w, int h, px c)
{
    int x0 = clampi(x, 0, SCR_W), x1 = clampi(x + w, 0, SCR_W);
    int y0 = clampi(y, b->y0, b->y1), y1 = clampi(y + h, b->y0, b->y1);
    for (int yy = y0; yy < y1; yy++) {
        px *p = band_row(b, yy) + x0;
        for (int xx = x0; xx < x1; xx++) *p++ = c;
    }
}

void gfx_hline(const band *b, int x, int y, int w, px c) { gfx_fill(b, x, y, w, 1, c); }
void gfx_vline(const band *b, int x, int y, int h, px c) { gfx_fill(b, x, y, 1, h, c); }

void gfx_frame(const band *b, int x, int y, int w, int h, int t, px c)
{
    gfx_fill(b, x, y, w, t, c);
    gfx_fill(b, x, y + h - t, w, t, c);
    gfx_fill(b, x, y + t, t, h - 2 * t, c);
    gfx_fill(b, x + w - t, y + t, t, h - 2 * t, c);
}

/* отсечение Коэна–Сазерленда: без него улетевшая за экран линия гоняет
 * Брезенхэма десятки тысяч шагов */
static int outcode(int x, int y, int ylo, int yhi)
{
    return (x < 0) | ((x >= SCR_W) << 1) | ((y < ylo) << 2) | ((y >= yhi) << 3);
}

static int clip_line(int *x0, int *y0, int *x1, int *y1, int ylo, int yhi)
{
    int c0 = outcode(*x0, *y0, ylo, yhi), c1 = outcode(*x1, *y1, ylo, yhi);
    for (int guard = 0; guard < 8; guard++) {
        if (!(c0 | c1)) return 1;
        if (c0 & c1) return 0;
        int c = c0 ? c0 : c1, x, y;
        if (c & 8)      { y = yhi - 1; x = *x0 + (*x1 - *x0) * (y - *y0) / (*y1 - *y0); }
        else if (c & 4) { y = ylo;     x = *x0 + (*x1 - *x0) * (y - *y0) / (*y1 - *y0); }
        else if (c & 2) { x = SCR_W - 1; y = *y0 + (*y1 - *y0) * (x - *x0) / (*x1 - *x0); }
        else            { x = 0;         y = *y0 + (*y1 - *y0) * (x - *x0) / (*x1 - *x0); }
        if (c == c0) { *x0 = x; *y0 = y; c0 = outcode(x, y, ylo, yhi); }
        else         { *x1 = x; *y1 = y; c1 = outcode(x, y, ylo, yhi); }
    }
    return 0;
}

void gfx_line(const band *b, int x0, int y0, int x1, int y1, px c)
{
    if (!clip_line(&x0, &y0, &x1, &y1, b->y0, b->y1)) return;
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        if ((unsigned)x0 < SCR_W && y0 >= b->y0 && y0 < b->y1) band_row(b, y0)[x0] = c;
        if (x0 == x1 && y0 == y1) break;
        int e2 = err * 2;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void gfx_disc(const band *b, int cx, int cy, int r, px c)
{
    int y0 = clampi(cy - r, b->y0, b->y1), y1 = clampi(cy + r + 1, b->y0, b->y1);
    for (int y = y0; y < y1; y++) {
        int dy = y - cy, d = r * r - dy * dy;
        if (d < 0) continue;
        int hw = 0;
        while ((hw + 1) * (hw + 1) <= d) hw++;
        gfx_fill(b, cx - hw, y, 2 * hw + 1, 1, c);
    }
}

void gfx_ring(const band *b, int cx, int cy, int r, int t, px c)
{
    int ri = r - t;
    int y0 = clampi(cy - r, b->y0, b->y1), y1 = clampi(cy + r + 1, b->y0, b->y1);
    for (int y = y0; y < y1; y++) {
        int dy = y - cy, d = r * r - dy * dy, di = ri * ri - dy * dy;
        if (d < 0) continue;
        int ho = 0; while ((ho + 1) * (ho + 1) <= d) ho++;
        int hi = 0; if (di > 0) while ((hi + 1) * (hi + 1) <= di) hi++;
        if (di <= 0) { gfx_fill(b, cx - ho, y, 2 * ho + 1, 1, c); continue; }
        gfx_fill(b, cx - ho, y, ho - hi, 1, c);
        gfx_fill(b, cx + hi + 1, y, ho - hi, 1, c);
    }
}

void gfx_round(const band *b, int x, int y, int w, int h, int r, px c)
{
    int y0 = clampi(y, b->y0, b->y1), y1 = clampi(y + h, b->y0, b->y1);
    for (int yy = y0; yy < y1; yy++) {
        int cut = 0, dy = 0;
        if (yy < y + r)          dy = y + r - yy;
        else if (yy >= y + h - r) dy = yy - (y + h - r) + 1;
        if (dy) {
            int d = r * r - dy * dy, hw = 0;
            if (d < 0) continue;
            while ((hw + 1) * (hw + 1) <= d) hw++;
            cut = r - hw;
        }
        gfx_fill(b, x + cut, yy, w - 2 * cut, 1, c);
    }
}

void gfx_dim(const band *b, int x, int y, int w, int h, int q)
{
    int x0 = clampi(x, 0, SCR_W), x1 = clampi(x + w, 0, SCR_W);
    int y0 = clampi(y, b->y0, b->y1), y1 = clampi(y + h, b->y0, b->y1);
    for (int yy = y0; yy < y1; yy++) {
        px *p = band_row(b, yy) + x0;
        for (int xx = x0; xx < x1; xx++, p++) *p = px_scale(*p, q);
    }
}

void gfx_vgrad(const band *b, int x, int y, int w, int h, px top, px bot)
{
    int y0 = clampi(y, b->y0, b->y1), y1 = clampi(y + h, b->y0, b->y1);
    for (int yy = y0; yy < y1; yy++)
        gfx_fill(b, x, yy, w, 1, px_mix(top, bot, h > 1 ? (yy - y) * 256 / (h - 1) : 0));
}

void gfx_blit(const band *b, int x, int y, int w, int h, const px *src)
{
    int x0 = clampi(x, 0, SCR_W), x1 = clampi(x + w, 0, SCR_W);
    int y0 = clampi(y, b->y0, b->y1), y1 = clampi(y + h, b->y0, b->y1);
    for (int yy = y0; yy < y1; yy++) {
        const px *s = src + (yy - y) * w + (x0 - x);
        px *p = band_row(b, yy) + x0;
        for (int xx = x0; xx < x1; xx++) *p++ = *s++;
    }
}

void gfx_blit8(const band *b, int x, int y, int w, int h, const uint8_t *src,
               const px *pal, int key)
{
    int x0 = clampi(x, 0, SCR_W), x1 = clampi(x + w, 0, SCR_W);
    int y0 = clampi(y, b->y0, b->y1), y1 = clampi(y + h, b->y0, b->y1);
    for (int yy = y0; yy < y1; yy++) {
        const uint8_t *s = src + (yy - y) * w + (x0 - x);
        px *p = band_row(b, yy) + x0;
        if (key < 0) for (int xx = x0; xx < x1; xx++) *p++ = pal[*s++];
        else for (int xx = x0; xx < x1; xx++, p++, s++) if (*s != key) *p = pal[*s];
    }
}

void gfx_blit8_scaled(const band *b, int dx, int dy, int dw, int dh,
                      const uint8_t *src, int sw, int sh, const px *pal, int key, int dimq)
{
    if (dw <= 0 || dh <= 0) return;
    int x0 = clampi(dx, 0, SCR_W), x1 = clampi(dx + dw, 0, SCR_W);
    int y0 = clampi(dy, b->y0, b->y1), y1 = clampi(dy + dh, b->y0, b->y1);
    int stepx = (sw << 16) / dw, stepy = (sh << 16) / dh;
    for (int yy = y0; yy < y1; yy++) {
        int v = (yy - dy) * stepy;
        const uint8_t *row = src + (v >> 16) * sw;
        int u = (x0 - dx) * stepx;
        px *p = band_row(b, yy) + x0;
        for (int xx = x0; xx < x1; xx++, p++, u += stepx) {
            uint8_t i = row[u >> 16];
            if (key >= 0 && i == key) continue;
            *p = dimq >= 256 ? pal[i] : px_scale(pal[i], dimq);
        }
    }
}
