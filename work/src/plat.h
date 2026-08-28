/* plat.h — то немногое, что игра знает о железе.
 *
 * Один и тот же код игр собирается и в прошивку часов-таймера, и в десктопный
 * стенд (host/host_sdl.c).  Различия целиком спрятаны здесь.
 */
#ifndef PLAT_H
#define PLAT_H

#include <stdint.h>
#include <stddef.h>

#define SCR_W 240
#define SCR_H 240
#define BAND_H 240           /* максимум: если ОЗУ хватает, кадр рисуется целиком */
extern int g_band_h;         /* фактическая: сколько ОЗУ нашлось */

/* Пиксель — RGB565, старшим байтом вперёд: ровно так его глотает ST7789. */
typedef uint16_t px;
#define RGB565_(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
#define BSWAP16_(v)      ((uint16_t)((((v) >> 8) & 0xFF) | (((v) & 0xFF) << 8)))
#define RGB(r, g, b)     ((px)BSWAP16_(RGB565_(r, g, b)))
#define BLACK  RGB(0, 0, 0)
#define WHITE  RGB(255, 255, 255)

static inline px  px_pack(int r, int g, int b) { return RGB(r, g, b); }
static inline void px_unpack(px c, int *r, int *g, int *b)
{
    uint16_t v = BSWAP16_(c);
    *r = (v >> 8) & 0xF8; *g = (v >> 3) & 0xFC; *b = (v << 3) & 0xF8;
}
/* c * q / 256 — затемнение (отражения в cover flow, «туман» в 3D) */
px px_scale(px c, int q);
px px_mix(px a, px b, int t);   /* t = 0..256 */

/* --- кадр полосами -------------------------------------------------------
 * Кадрового буфера на 240x240 в ОЗУ нет: рисуем полосу, отдаём её в SPI,
 * берём следующую.  Игра рисует сцену столько раз, сколько полос.
 */
typedef struct { px *p; int y0, y1; } band;
static inline px *band_row(const band *b, int y) { return b->p + (y - b->y0) * SCR_W; }

void  fb_begin(void);          /* начать кадр */
band *fb_next(void);           /* отдать нарисованное, взять следующую полосу; NULL = кадр кончился */
void  fb_rows(int y0, int y1); /* отправлять на экран только эти строки (на кадр) */

/* --- кнопки --------------------------------------------------------------
 * У таймера их четыре.  Раскладка (какой байт какая кнопка) уточняется
 * на живом железе приложением «Кнопки», отсюда btn_map[].
 */
enum { BTN_UP = 0, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_N };
#define B_UP    (1u << BTN_UP)
#define B_DOWN  (1u << BTN_DOWN)
#define B_LEFT  (1u << BTN_LEFT)
#define B_RIGHT (1u << BTN_RIGHT)
#define B_ALL   (B_UP | B_DOWN | B_LEFT | B_RIGHT)

void     in_poll(void);        /* раз за кадр */
uint32_t in_held(void);        /* маска зажатых */
uint32_t in_hit(void);         /* маска нажатых с прошлого опроса */
uint32_t in_rep(void);         /* то же, но с автоповтором */
uint32_t in_held_ms(int btn);  /* сколько миллисекунд кнопка зажата */
void     in_set_map(int logical, int raw);
int      in_get_map(int logical);
const volatile uint8_t *in_raw(void);   /* 4 байта состояния из прошивки; 0 на стенде */
uint32_t plat_cycles(void);
uint32_t plat_cyc_per_ms(void);
uint32_t plat_ticks(void);

/* --- память --------------------------------------------------------------
 * Кучи у нас нет: вся оперативка игры — кусок стека нашей задачи, раздаваемый
 * простым «указателем вверх».  Меню сбрасывает его перед каждым запуском.
 */
void  mem_init(void *base, int size);
void  mem_add(void *base, int size);
int   mem_biggest(void);
void *mem_alloc(int n);        /* выровнено на 4, обнулено; 0 если не влезло */
void  mem_lock(void);
void  mem_reset(void);
int   mem_left(void);

/* --- настройки, переживающие перезагрузку -------------------------------- */
enum { CFG_SND, CFG_MUTE, CFG_INIT, CFG_N };
int  cfg_get(int id);
void cfg_set(int id, int v);

/* --- язык ----------------------------------------------------------------
 * Образ собирается один, язык выбирается в Studio и лежит в таблице ZealMod.
 * Поэтому надписи выбираются на ходу: TR("Score", "Счёт").
 */
enum { ZM_EN = 0, ZM_RU = 1, ZM_LANGS };
int zm_lang(void);
const char *zm_tr(const char *en, const char *ru);
#define TR(en, ru) zm_tr((en), (ru))
/* для массивов надписей: TRA(en_array, ru_array)[i] */
#define TRA(en, ru) (zm_lang() == ZM_RU ? (ru) : (en))

/* --- время и прочее ------------------------------------------------------ */
uint32_t now_ms(void);
void     plat_sleep_ms(uint32_t ms);
void     plat_yield(void);
uint32_t plat_rand(void);
void     plat_srand(uint32_t seed);
void     plat_exit_to_stock(void);   /* выход из мода обратно в таймер */
void     plat_log(const char *fmt, ...);

/* --- примитивы ----------------------------------------------------------- */
void gfx_clear(const band *b, px c);
void gfx_fill(const band *b, int x, int y, int w, int h, px c);
void gfx_frame(const band *b, int x, int y, int w, int h, int t, px c);
void gfx_hline(const band *b, int x, int y, int w, px c);
void gfx_vline(const band *b, int x, int y, int h, px c);
void gfx_line(const band *b, int x0, int y0, int x1, int y1, px c);
void gfx_disc(const band *b, int cx, int cy, int r, px c);
void gfx_ring(const band *b, int cx, int cy, int r, int t, px c);
void gfx_round(const band *b, int x, int y, int w, int h, int r, px c);
void gfx_dim(const band *b, int x, int y, int w, int h, int q);
void gfx_vgrad(const band *b, int x, int y, int w, int h, px top, px bot);
/* 16-битная картинка как есть */
void gfx_blit(const band *b, int x, int y, int w, int h, const px *src);
/* 8-битная палитровая; key < 0 — без прозрачности */
void gfx_blit8(const band *b, int x, int y, int w, int h, const uint8_t *src,
               const px *pal, int key);
/* она же с растяжением (16.16) */
void gfx_blit8_scaled(const band *b, int dx, int dy, int dw, int dh,
                      const uint8_t *src, int sw, int sh, const px *pal, int key, int dimq);

/* --- текст ---------------------------------------------------------------
 * Шрифт — атлас 8bpp с покрытием (сглаженный), метрики рядом.  UTF-8,
 * латиница + кириллица.
 */
typedef struct {
    const uint8_t *bitmap;   /* 8bpp alpha */
    const uint8_t *w;        /* ширина глифа */
    const uint8_t *h;
    const int8_t  *ox, *oy;  /* сдвиг от пера */
    const uint8_t *adv;      /* шаг пера */
    const uint32_t *off;     /* смещение глифа в bitmap */
    const uint16_t *cp;      /* кодовые точки, по возрастанию */
    int n, line, base;
} font_t;

int  gfx_text(const band *b, int x, int y, const font_t *f, px c, const char *s);
int  gfx_text_w(const font_t *f, const char *s);
int  gfx_text_c(const band *b, int cx, int y, const font_t *f, px c, const char *s);
void gfx_text_shadow(const band *b, int x, int y, const font_t *f, px c, px sh, const char *s);

extern const font_t font_s, font_m, font_l, font_xl;

/* --- приложение ---------------------------------------------------------- */
typedef struct {
    const char    *title;
    const uint8_t *cover;    /* 8bpp COVER_W x COVER_H */
    const px      *cover_pal;
    void         (*run)(void);
} app_t;

#define COVER_W 96
#define COVER_H 96

/* Пункт меню: одинаково выглядит и для встроенного приложения, и для модуля,
 * который Studio дописала в образ (там указатели лежат 32-битными числами). */
typedef struct {
    const char    *title;
    const uint8_t *cover;
    const px      *pal;
    void         (*run)(void);
    uint8_t        exit_btn;
    uint8_t        exit_hold;   /* десятые доли секунды */
} zm_item_t;

extern const app_t *const apps[];
extern const int    apps_n;

void menu_run(void);         /* cover flow */

#endif /* PLAT_H */
