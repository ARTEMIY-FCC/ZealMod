/* wallclock.c — время суток. Прошивка своего времени нам не отдаёт (символа
 * не нашли), поэтому время выставляется руками и живёт в RTC-памяти: мягкую
 * перезагрузку переживает, отключение питания — нет. */
#include "game.h"
#include "snd.h"
#include "nvram.h"
#include "wallclock.h"

typedef struct { uint32_t set_ms, epoch_sec; uint8_t valid; } clk_t;
#ifdef PLAT_HOST
static clk_t clk;
#else
__attribute__((section(".persist"))) static clk_t clk;
#endif

static const uint8_t mdays[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
/* Названия месяцев и дней: язык выбирается на ходу, поэтому не массивы, а
 * функции — их видно и модулям (календарь, часы). */
static const char *const month_en[12] = { "January", "February", "March", "April",
    "May", "June", "July", "August", "September", "October", "November", "December" };
static const char *const month_ru[12] = { "январь", "февраль", "март", "апрель", "май",
    "июнь", "июль", "август", "сентябрь", "октябрь", "ноябрь", "декабрь" };
/* в русском дата требует родительного падежа: «5 января» */
static const char *const month_gen[12] = { "января", "февраля", "марта", "апреля", "мая",
    "июня", "июля", "августа", "сентября", "октября", "ноября", "декабря" };
static const char *const wday_en[7] = { "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun" };
static const char *const wday_ru[7] = { "пн", "вт", "ср", "чт", "пт", "сб", "вс" };

const char *wc_month(int m) { return TRA(month_en, month_ru)[m % 12]; }
const char *wc_month_gen(int m) { return TRA(month_en, month_gen)[m % 12]; }
const char *wc_wday(int d) { return TRA(wday_en, wday_ru)[d % 7]; }

static int leap(int y) { return (!(y % 4) && (y % 100)) || !(y % 400); }
static int mlen(int y, int m) { return m == 1 && leap(y) ? 29 : mdays[m]; }

/* минуты от 2000-01-01 00:00 */
uint32_t wc_days_from_civil(int y, int m, int d)
{
    uint32_t days = 0;
    for (int yy = 2000; yy < y; yy++) days += leap(yy) ? 366 : 365;
    for (int mm = 0; mm < m; mm++) days += mlen(y, mm);
    return days + (uint32_t)(d - 1);
}

void wc_civil_from_days(uint32_t days, int *y, int *m, int *d)
{
    int yy = 2000;
    for (;;) {
        uint32_t n = leap(yy) ? 366 : 365;
        if (days < n) break;
        days -= n; yy++;
    }
    int mm = 0;
    for (;;) {
        uint32_t n = (uint32_t)mlen(yy, mm);
        if (days < n) break;
        days -= n; mm++;
    }
    *y = yy; *m = mm; *d = (int)days + 1;
}

int wc_valid(void) { return clk.valid; }

/* Секунды копим в RTC-памяти: она переживает мягкую перезагрузку, а счётчик
 * миллисекунд после неё начинается заново.  Досчитываем каждый кадр — теряется
 * меньше секунды. */
void wc_tick(void)
{
    if (!clk.valid) return;
    uint32_t d = now_ms() - clk.set_ms;
    if (d > 3600000u) {          /* счётчик миллисекунд начался заново (перезагрузка) */
        clk.set_ms = now_ms();
        return;
    }
    if (d >= 1000) {
        clk.epoch_sec += d / 1000;
        clk.set_ms += (d / 1000) * 1000;
    }
}

void wc_boot(void) { clk.set_ms = now_ms(); }   /* новая загрузка — новый отсчёт */

uint32_t wc_epoch(void) { wc_tick(); return clk.valid ? clk.epoch_sec : 0; }

void wc_set_epoch(uint32_t sec)
{
    if (!sec) return;
    clk.epoch_sec = sec;
    clk.set_ms = now_ms();
    clk.valid = 1;
}

wc_time wc_now(void)
{
    wc_time t;
    wc_tick();
    uint32_t secs = clk.epoch_sec + (now_ms() - clk.set_ms) / 1000u;
    uint32_t mins = secs / 60u;
    uint32_t days = mins / 1440u;
    t.sec = (int)(secs % 60);
    t.min = (int)(mins % 60);
    t.hour = (int)(mins / 60 % 24);
    wc_civil_from_days(days, &t.year, &t.mon, &t.day);
    t.wday = (int)((days + 5) % 7);          /* 2000-01-01 — суббота */
    t.valid = clk.valid;
    return t;
}

void wc_set(int y, int mo, int d, int h, int mi)
{
    clk.epoch_sec = (wc_days_from_civil(y, mo, d) * 1440u + (uint32_t)h * 60u + (uint32_t)mi) * 60u;
    clk.set_ms = now_ms();
    clk.valid = 1;
}

/* ---- экран установки ---------------------------------------------------
 * Полей шесть: часы, минуты, день, месяц, год и кнопка «Сохранить» — аккордов
 * из двух кнопок железо не даёт, так что выход должен быть явным.
 */
int wc_setup(void)
{
    game_exit_button(BTN_LEFT);        /* ◀ только переключает поле — держать безопасно */
    wc_time t = wc_now();
    if (!t.valid) { t.year = 2026; t.mon = 0; t.day = 1; t.hour = 12; t.min = 0; }
    int f = 0;
    const int nf = 6, SAVE = 5;
    uint32_t last = now_ms();
    for (;;) {
        in_poll();
        if (game_quit()) { game_exit_button(BTN_DOWN); return 0; }
        uint32_t h = in_rep(), hh = in_hit();
        if (hh & B_LEFT) f = f ? f - 1 : nf - 1;
        if (hh & B_RIGHT) f = (f + 1) % nf;
        if (f == SAVE && (hh & (B_UP | B_DOWN))) {      /* сохранить и выйти */
            wc_set(t.year, t.mon, t.day, t.hour, t.min);
            nv_dirty();     /* во флеш это уедет при следующей загрузке мода */
            SND(snd_pick);
            game_exit_button(BTN_DOWN);
            return 1;
        }
        int d = (h & B_UP) ? 1 : (h & B_DOWN) ? -1 : 0;
        if (d) {
            switch (f) {
            case 0: t.hour = (t.hour + d + 24) % 24; break;
            case 1: t.min = (t.min + d + 60) % 60; break;
            case 2: t.day = (t.day - 1 + d + 31) % 31 + 1; break;
            case 3: t.mon = (t.mon + d + 12) % 12; break;
            case 4: t.year += d; break;
            }
            SND(snd_click);
        }
        if (t.day > mlen(t.year, t.mon)) t.day = mlen(t.year, t.mon);
        wc_set(t.year, t.mon, t.day, t.hour, t.min);

        fb_begin();
        for (band *b; (b = fb_next()); ) {
            char s[48];
            px sel = RGB(255, 210, 80);
            gfx_vgrad(b, 0, 0, SCR_W, SCR_H, RGB(20, 22, 32), RGB(6, 6, 12));
            gfx_text_c(b, 120, 34, &font_m, RGB(150, 160, 190), TR("set the time", "выставить время"));
            fx_fmt(s, sizeof s, "%02d", t.hour);
            gfx_text_c(b, 84, 96, &font_l, f == 0 ? sel : WHITE, s);
            gfx_text_c(b, 120, 96, &font_l, WHITE, ":");
            fx_fmt(s, sizeof s, "%02d", t.min);
            gfx_text_c(b, 156, 96, &font_l, f == 1 ? sel : WHITE, s);
            fx_fmt(s, sizeof s, "%d", t.day);
            gfx_text_c(b, 54, 140, &font_m, f == 2 ? sel : WHITE, s);
            gfx_text_c(b, 118, 140, &font_m, f == 3 ? sel : WHITE, wc_month(t.mon));
            fx_fmt(s, sizeof s, "%d", t.year);
            gfx_text_c(b, 196, 140, &font_m, f == 4 ? sel : WHITE, s);
            gfx_round(b, 60, 160, 120, 34, 8, f == SAVE ? RGB(60, 130, 70) : RGB(30, 34, 46));
            gfx_text_c(b, 120, 182, &font_m, f == SAVE ? WHITE : RGB(140, 146, 166), TR("Save", "Сохранить"));
            gfx_text_c(b, 120, 214, &font_s, RGB(110, 118, 140),
                       f == SAVE ? TR("▲ save and exit", "▲ сохранить и выйти")
                                 : TR("◀▶ field   ▲▼ value", "◀▶ поле   ▲▼ значение"));
        }
        game_frame_wait(&last, 33);
    }
}
