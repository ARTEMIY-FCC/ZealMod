/* splash.c — заставка ZealMod: то, что видно первым при входе в мод.
 *
 * Рисуется без картинок: знак собран из прямоугольников, поэтому во флеше
 * ничего не занимает.  Тема может подсунуть свою картинку (theme.logo) — её
 * готовит Studio из PNG внутри .zt.
 */
#include "plat.h"
#include "fx.h"
#include "zmtab.h"

#define IN_MS   260
#define HOLD_MS 620
#define OUT_MS  220

/* стилизованная Z в скруглённом квадрате */
static void sign(const band *b, int cx, int cy, int s, int q, px face, px ink)
{
    int r = s / 2;
    gfx_round(b, cx - r, cy - r, s, s, s / 5, px_scale(face, q));
    int w = s * 56 / 100, t = s * 13 / 100;
    int x0 = cx - w / 2, x1 = cx + w / 2, y0 = cy - w / 2, y1 = cy + w / 2 - t;
    px c = px_scale(ink, q);
    gfx_fill(b, x0, y0, w, t, c);                       /* верхняя перекладина */
    gfx_fill(b, x0, y1, w, t, c);                       /* нижняя */
    for (int i = 0; i < w; i++) {                       /* диагональ */
        int x = x1 - 1 - i;
        int y = y0 + (y1 - y0) * i / (w - 1);
        gfx_fill(b, x - t / 2, y, t, t, c);
    }
}

static void frame(const band *b, int q, const zm_theme_t *th)
{
    gfx_clear(b, px_scale(px_mix(th->bg_bot, BLACK, 128), q));
    if (th->logo && th->logo_pal) {
        int w = th->logo_w, h = th->logo_h;
        int dw = w, dh = h;
        if (dw > 160) { dh = dh * 160 / dw; dw = 160; }
        gfx_blit8_scaled(b, (SCR_W - dw) / 2, 46 + (120 - dh) / 2, dw, dh,
                         (const uint8_t *)(uintptr_t)th->logo, w, h,
                         (const px *)(uintptr_t)th->logo_pal, -1, q);
    } else {
        sign(b, SCR_W / 2, 104, 96, q, th->accent, th->bg_bot);
    }
    gfx_text_c(b, SCR_W / 2, 186, &font_l, px_scale(th->text, q), th->name);
    gfx_text_c(b, SCR_W / 2, 212, &font_s, px_scale(th->text_dim, q), zm_build());
}

void zm_splash(void)
{
    const zm_theme_t *th = zm_theme();
    if (!zm_cfg()->splash) return;
    uint32_t t0 = now_ms();
    for (;;) {
        uint32_t el = now_ms() - t0;
        int q;
        if (el < IN_MS) q = (int)(el * 256 / IN_MS);
        else if (el < IN_MS + HOLD_MS) q = 256;
        else if (el < IN_MS + HOLD_MS + OUT_MS)
            q = 256 - (int)((el - IN_MS - HOLD_MS) * 256 / OUT_MS);
        else break;
        in_poll();
        if (in_hit()) break;                 /* нажатие пропускает заставку */
        fb_begin();
        for (band *b; (b = fb_next()); ) frame(b, iclamp(q, 0, 256), th);
        plat_sleep_ms(16);
    }
}
