/* diag.c — то, без чего первый запуск на железе превращается в гадание:
 * что за кнопки, куда смотрит экран, с какой скоростью бежит время. */
#include "game.h"
#include "wallclock.h"

static const char *btn_name(int i)
{
    static const char *const en[BTN_N] = { "UP", "DOWN", "LEFT", "RIGHT" };
    static const char *const ru[BTN_N] = { "ВВЕРХ", "ВНИЗ", "ВЛЕВО", "ВПРАВО" };
    return TRA(en, ru)[i & 3];
}

/* --- раскладка: просим нажать каждую кнопку и смотрим, какой байт ожил --- */
static void map_buttons(void)
{
    const volatile uint8_t *raw = in_raw();
    uint32_t last = now_ms();
    if (!raw) return;
    for (int i = 0; i < BTN_N; i++) {
        int got = -1;
        while (got < 0) {
            for (int k = 0; k < BTN_N; k++) if (raw[k]) got = k;
            fb_begin();
            for (band *b; (b = fb_next()); ) {
                gfx_clear(b, RGB(10, 12, 20));
                gfx_text_c(b, 120, 90, &font_m, RGB(150, 160, 190), TR("press a button", "нажми кнопку"));
                gfx_text_c(b, 120, 130, &font_l, RGB(255, 210, 80), btn_name(i));
                char s[32];
                fx_fmt(s, sizeof s, "%d %d %d %d", raw[0], raw[1], raw[2], raw[3]);
                gfx_text_c(b, 120, 190, &font_s, RGB(90, 96, 116), s);
            }
            game_frame_wait(&last, 33);
        }
        in_set_map(i, got);
        while (raw[got]) { plat_sleep_ms(20); }   /* дождаться отпускания */
        plat_sleep_ms(150);
    }
}

/* --- проверка экрана: геометрия, порядок байт, градиенты ---------------- */
static void screen_test(const band *b)
{
    static const px bars[8] = { RGB(255,255,255), RGB(255,255,0), RGB(0,255,255), RGB(0,255,0),
                                RGB(255,0,255), RGB(255,0,0), RGB(0,0,255), RGB(0,0,0) };
    for (int i = 0; i < 8; i++) gfx_fill(b, i * 30, 0, 30, 90, bars[i]);
    gfx_vgrad(b, 0, 90, 80, 60, RGB(255, 0, 0), RGB(0, 0, 0));
    gfx_vgrad(b, 80, 90, 80, 60, RGB(0, 255, 0), RGB(0, 0, 0));
    gfx_vgrad(b, 160, 90, 80, 60, RGB(0, 0, 255), RGB(0, 0, 0));
    gfx_fill(b, 0, 150, SCR_W, 90, RGB(16, 16, 22));
    gfx_frame(b, 0, 0, SCR_W, SCR_H, 2, RGB(255, 255, 255));
    gfx_fill(b, 2, 2, 20, 20, RGB(255, 0, 0));          /* левый верх — красный */
    gfx_fill(b, SCR_W - 22, SCR_H - 22, 20, 20, RGB(0, 255, 0)); /* правый низ — зелёный */
    gfx_text_c(b, 120, 178, &font_m, WHITE, TR("red = top left", "красный — левый верх"));
    gfx_text_c(b, 120, 200, &font_s, RGB(170, 175, 195), TR("green = bottom right", "зелёный — правый низ"));
    gfx_text_c(b, 120, 224, &font_s, RGB(120, 126, 146), TR("▶ next", "▶ дальше"));
}

void run_diag(void)
{
    game_exit_button(BTN_DOWN);
    int page = 0;
    uint32_t last = now_ms(), t0 = now_ms(), frames = 0, fps = 0, fps_t = now_ms();
    while (!game_quit()) {
        in_poll();
        if (in_hit() & B_RIGHT) page = (page + 1) % 3;
        if (in_hit() & B_LEFT) page = (page + 2) % 3;
        if ((in_hit() & B_UP) && page == 0) { map_buttons(); last = now_ms(); }
        frames++;
        if (now_ms() - fps_t >= 1000) { fps = frames; frames = 0; fps_t = now_ms(); }
        const volatile uint8_t *raw = in_raw();
        char s[48];
        fb_begin();
        for (band *b; (b = fb_next()); ) {
            if (page == 2) { screen_test(b); continue; }
            gfx_vgrad(b, 0, 0, SCR_W, SCR_H, RGB(14, 16, 26), RGB(4, 4, 10));
            gfx_text_c(b, 120, 24, &font_m, RGB(120, 200, 255),
                       page ? TR("system", "система") : TR("buttons", "кнопки"));
            if (page == 0) {
                for (int i = 0; i < BTN_N; i++) {
                    int on = (in_held() >> i) & 1;
                    gfx_round(b, 20, 44 + i * 34, 200, 28, 6, on ? RGB(40, 120, 60) : RGB(24, 26, 38));
                    gfx_text(b, 32, 64 + i * 34, &font_s, on ? WHITE : RGB(140, 146, 166), btn_name(i));
                    fx_fmt(s, sizeof s, TR("byte %d", "байт %d"), in_get_map(i));
                    gfx_text(b, 140, 64 + i * 34, &font_s, RGB(140, 146, 166), s);
                }
                if (raw) fx_fmt(s, sizeof s, TR("raw: %d %d %d %d", "сырые: %d %d %d %d"), raw[0], raw[1], raw[2], raw[3]);
                else fx_fmt(s, sizeof s, TR("desktop: arrow keys", "стенд: клавиши-стрелки"));
                gfx_text_c(b, 120, 196, &font_s, RGB(110, 116, 136), s);
                gfx_text_c(b, 120, 222, &font_s, RGB(255, 210, 80), TR("▲ remap", "▲ переназначить"));
            } else {
                fx_fmt(s, sizeof s, TR("cycles per ms: %d", "тактов в мс: %d"), (int)plat_cyc_per_ms());
                gfx_text(b, 20, 62, &font_s, WHITE, s);
                fx_fmt(s, sizeof s, TR("frames per second: %d", "кадров в секунду: %d"), (int)fps);
                gfx_text(b, 20, 86, &font_s, WHITE, s);
                fx_fmt(s, sizeof s, TR("free RAM: %d B", "свободно ОЗУ: %d Б"), mem_left());
                gfx_text(b, 20, 110, &font_s, WHITE, s);
                fx_fmt(s, sizeof s, TR("in mod: %d s", "в моде: %d с"), (int)((now_ms() - t0) / 1000));
                gfx_text(b, 20, 134, &font_s, WHITE, s);
                wc_time t = wc_now();
                fx_fmt(s, sizeof s, TR("clock: %02d:%02d %s", "часы: %02d:%02d %s"), t.hour, t.min, t.valid ? "" : TR("(not set)", "(не заданы)"));
                gfx_text(b, 20, 158, &font_s, WHITE, s);
                gfx_text(b, 20, 190, &font_s, RGB(140, 146, 166), TR("◀▶ pages", "◀▶ страницы"));
            }
        }
        game_frame_wait(&last, 33);
    }
}
