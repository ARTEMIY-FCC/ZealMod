/* text.c — UTF-8 поверх атласа 4bpp со сглаживанием. */
#include "plat.h"

static const uint8_t *glyph(const font_t *f, uint32_t cp, int *w, int *h, int *ox, int *oy, int *adv)
{
    int lo = 0, hi = f->n - 1;
    while (lo <= hi) {
        int m = (lo + hi) >> 1;
        if (f->cp[m] < cp) lo = m + 1;
        else if (f->cp[m] > cp) hi = m - 1;
        else {
            *w = f->w[m]; *h = f->h[m]; *ox = f->ox[m]; *oy = f->oy[m]; *adv = f->adv[m];
            return f->bitmap + f->off[m];
        }
    }
    return 0;
}

/* строку могли обрезать посреди многобайтового символа — за конец не шагаем */
static const char *utf8(const char *s, uint32_t *cp)
{
    uint8_t c = (uint8_t)*s++;
    if (c < 0x80) { *cp = c; return s; }
    if ((c & 0xE0) == 0xC0) {
        if (!s[0]) { *cp = '?'; return s; }
        *cp = ((c & 0x1F) << 6) | (s[0] & 0x3F);
        return s + 1;
    }
    if ((c & 0xF0) == 0xE0) {
        if (!s[0] || !s[1]) { *cp = '?'; return s + (s[0] ? 1 : 0); }
        *cp = ((c & 0x0F) << 12) | ((s[0] & 0x3F) << 6) | (s[1] & 0x3F);
        return s + 2;
    }
    *cp = '?';
    return s;
}

int gfx_text_w(const font_t *f, const char *s)
{
    int x = 0, w, h, ox, oy, adv;
    uint32_t cp;
    while (*s) {
        s = utf8(s, &cp);
        if (glyph(f, cp, &w, &h, &ox, &oy, &adv)) x += adv;
        else if (cp == ' ') x += f->line / 3;
    }
    return x;
}

/* x — левый край, y — базовая линия */
int gfx_text(const band *b, int x, int y, const font_t *f, px c, const char *s)
{
    int gw, gh, ox, oy, adv;
    uint32_t cp;
    while (*s) {
        s = utf8(s, &cp);
        const uint8_t *bm = glyph(f, cp, &gw, &gh, &ox, &oy, &adv);
        if (!bm) { if (cp == ' ') x += f->line / 3; continue; }
        int gx = x + ox, gy = y + oy, stride = (gw + 1) >> 1;
        int yy0 = gy < b->y0 ? b->y0 : gy, yy1 = gy + gh > b->y1 ? b->y1 : gy + gh;
        for (int yy = yy0; yy < yy1; yy++) {
            const uint8_t *row = bm + (yy - gy) * stride;
            px *p = band_row(b, yy);
            for (int i = 0; i < gw; i++) {
                int px_x = gx + i;
                if ((unsigned)px_x >= SCR_W) continue;
                int a = (i & 1) ? (row[i >> 1] & 0xF) : (row[i >> 1] >> 4);
                if (!a) continue;
                p[px_x] = a == 15 ? c : px_mix(p[px_x], c, a * 17);
            }
        }
        x += adv;
    }
    return x;
}

int gfx_text_c(const band *b, int cx, int y, const font_t *f, px c, const char *s)
{
    return gfx_text(b, cx - gfx_text_w(f, s) / 2, y, f, c, s);
}

void gfx_text_shadow(const band *b, int x, int y, const font_t *f, px c, px sh, const char *s)
{
    gfx_text(b, x + 1, y + 1, f, sh, s);
    gfx_text(b, x, y, f, c, s);
}
