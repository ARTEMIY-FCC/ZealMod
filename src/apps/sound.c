/* sound.c — настройки звука.  Прошивка постоянно гонит на ножку 21 свой
 * сигнал для внешнего дисплея — в наушниках это ровный треск, поэтому её
 * можно у прошивки забрать. */
#include "game.h"
#include "snd.h"

void run_sound(void)
{
    game_exit_button(BTN_DOWN);
    int sel = 0, probe = 0;
    uint32_t last = now_ms(), played = 0;
    while (!game_quit()) {
        in_poll();
        uint32_t h = in_hit();
        if (h & B_RIGHT) { sel = (sel + 1) % 4; SND(snd_click); }
        if (h & B_LEFT) { sel = (sel + 3) % 4; SND(snd_click); }
        if (h & B_UP) {
            if (sel == 0) { cfg_set(CFG_SND, !cfg_get(CFG_SND)); SND(snd_pick); }
            else if (sel == 1) cfg_set(CFG_MUTE, !cfg_get(CFG_MUTE));
            else if (sel == 2) {
                static const snote tune[] = { { 523, 90 }, { 659, 90 }, { 784, 90 },
                                              { 1047, 120 }, { 784, 90 }, { 1047, 200 } };
                SND(tune);
                played = now_ms();
            } else {
                /* ищем правый канал: гоняем ноту по другим ножкам */
                static const uint8_t cand[] = { 20, 10, 7, 6, 1, 0 };
                snd_probe(cand[probe % (int)(sizeof cand)], 880, 250);
                probe++;
                played = now_ms();
            }
        }
        static const uint8_t cand_pins[] = { 20, 10, 7, 6, 1, 0 };
        char right[32];
        fx_fmt(right, sizeof right, TR("Right channel: pin %d", "Правый канал: ножка %d"),
               cand_pins[probe % (int)(sizeof cand_pins)]);
        const char *names[4];
        names[0] = TR("Game sound", "Звук в играх");
        names[1] = TR("Mute timer buzz", "Глушить треск таймера");
        names[2] = TR("Test", "Проверить");
        names[3] = right;
        fb_begin();
        for (band *b; (b = fb_next()); ) {
            gfx_vgrad(b, 0, 0, SCR_W, SCR_H, RGB(20, 24, 34), RGB(6, 7, 12));
            gfx_text_c(b, 120, 32, &font_m, RGB(120, 200, 255), TR("sound", "звук"));
            for (int i = 0; i < 4; i++) {
                int y = 44 + i * 40;
                gfx_round(b, 12, y, 216, 34, 8, i == sel ? RGB(34, 44, 62) : RGB(22, 26, 36));
                gfx_text(b, 22, y + 22, &font_s, i == sel ? WHITE : RGB(150, 156, 176), names[i]);
                if (i < 2) {
                    int on = cfg_get(i == 0 ? CFG_SND : CFG_MUTE);
                    gfx_round(b, 180, y + 9, 34, 16, 8, on ? RGB(70, 180, 90) : RGB(60, 62, 74));
                    gfx_disc(b, on ? 206 : 188, y + 17, 6, WHITE);
                } else if (i == sel && now_ms() - played < 900) {
                    gfx_text(b, 196, y + 22, &font_s, RGB(255, 210, 80), "♪");
                }
            }
            gfx_text_c(b, 120, 214, &font_s, RGB(110, 116, 136), TR("◀▶ select   ▲ toggle", "◀▶ выбрать   ▲ переключить"));
            gfx_text_c(b, 120, 232, &font_s, RGB(90, 96, 116), TR("hold ▼ to exit", "выход — держать ▼"));
        }
        game_frame_wait(&last, 33);
    }
}
