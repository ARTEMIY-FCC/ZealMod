# Справочник ZealMod

[English version](../api.md)

Всё, что может звать программа. Заголовки лежат в `dist/sdk/include` (это те же
файлы, что `work/src/*.h`), и `zealmod pack` с `zealmod emu` подставляют их сами.

```c
#include "plat.h"   /* экран, кнопки, память, время, рисование, текст, язык */
#include "game.h"   /* выход из программы, кадры, рекорды, диалоги */
#include "fx.h"     /* целочисленная математика, форматирование, mem*/str* */
#include "snd.h"    /* звук */
```

У модуля одна точка входа (`zm_main`, имя берётся из `entry` в `module.json`),
и из неё надо вернуться, когда `game_quit()` станет единицей.

---

## Кадр

Экран 240×240, 16 бит на пиксель. Кадр рисуется полосами: сейчас полоса одна на
весь экран, но рассчитывать на это нельзя — рисуйте всю сцену внутри цикла.

```c
void  fb_begin(void);           /* начать кадр */
band *fb_next(void);            /* отдать полосу, взять следующую; NULL — конец */
void  fb_rows(int y0, int y1);  /* отправить на экран только эти строки */

typedef struct { px *p; int y0, y1; } band;
px *band_row(const band *b, int y);
```

```c
fb_begin();
for (band *b; (b = fb_next()); ) {
    gfx_clear(b, RGB(10, 12, 20));
    gfx_disc(b, 120, 120, 20, WHITE);
}
game_frame_wait(&last, 33);     /* ~30 кадров в секунду, и уступить системе */
```

`game_frame_wait()` обязателен: он и держит частоту, и отдаёт время. Без него
сторожевой таймер решит, что мод завис, и перезагрузит часы.

## Цвет

```c
typedef uint16_t px;                    /* RGB565 со сменой байт */
#define RGB(r, g, b)                    /* цвет из компонент 0..255 */
#define BLACK, WHITE
px  px_scale(px c, int q);              /* c * q / 256 — затемнить */
px  px_mix(px a, px b, int t);          /* t = 0..256 */
void px_unpack(px c, int *r, int *g, int *b);
```

## Рисование

```c
void gfx_clear (const band *b, px c);
void gfx_fill  (const band *b, int x, int y, int w, int h, px c);
void gfx_frame (const band *b, int x, int y, int w, int h, int t, px c);
void gfx_hline (const band *b, int x, int y, int w, px c);
void gfx_vline (const band *b, int x, int y, int h, px c);
void gfx_line  (const band *b, int x0, int y0, int x1, int y1, px c);
void gfx_disc  (const band *b, int cx, int cy, int r, px c);
void gfx_ring  (const band *b, int cx, int cy, int r, int t, px c);
void gfx_round (const band *b, int x, int y, int w, int h, int r, px c);
void gfx_dim   (const band *b, int x, int y, int w, int h, int q);
void gfx_vgrad (const band *b, int x, int y, int w, int h, px top, px bot);

void gfx_blit(const band *b, int x, int y, int w, int h, const px *src);
void gfx_blit8(const band *b, int x, int y, int w, int h, const uint8_t *src,
               const px *pal, int key);          /* key < 0 — без прозрачности */
void gfx_blit8_scaled(const band *b, int dx, int dy, int dw, int dh,
                      const uint8_t *src, int sw, int sh, const px *pal,
                      int key, int dimq);        /* dimq = 0..256 */
```

## Текст

Шрифт — сглаженный атлас, UTF-8, латиница и кириллица. `y` — базовая линия.

```c
int  gfx_text  (const band *b, int x, int y, const font_t *f, px c, const char *s);
int  gfx_text_c(const band *b, int cx, int y, const font_t *f, px c, const char *s);
int  gfx_text_w(const font_t *f, const char *s);
void gfx_text_shadow(const band *b, int x, int y, const font_t *f, px c, px sh,
                     const char *s);
extern const font_t font_s, font_m, font_l, font_xl;
```

## Язык

Образ собирается один и несёт оба языка; какой показывать — выбирают в Studio.

```c
enum { ZM_EN = 0, ZM_RU = 1 };
int         zm_lang(void);
const char *zm_tr(const char *en, const char *ru);
#define TR(en, ru)   zm_tr((en), (ru))
#define TRA(en, ru)  /* выбирает один из двух массивов: TRA(en, ru)[i] */
```

## Кнопки

Их четыре, и прошивка отдаёт только одну за раз — аккордов не бывает, поэтому
всё держится на долгих нажатиях.

```c
enum { BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_N };
#define B_UP, B_DOWN, B_LEFT, B_RIGHT, B_ALL

void     in_poll(void);            /* раз за кадр, до всех чтений */
uint32_t in_held(void);            /* маска зажатых сейчас */
uint32_t in_hit(void);             /* нажатые с прошлого опроса */
uint32_t in_rep(void);             /* то же, с автоповтором */
uint32_t in_held_ms(int btn);
void     in_set_map(int logical, int raw);
int      in_get_map(int logical);
const volatile uint8_t *in_raw(void);
```

## Выход из программы

```c
void game_exit_button(int btn);    /* какой кнопкой выходят (по умолчанию ▲) */
void game_exit_hold(int ms);       /* сколько её держать */
int  game_quit(void);              /* 1 = пора возвращаться из zm_main() */
```

Выбирайте кнопку, которую программа не держит подолгу по ходу дела. Значения по
умолчанию берутся из `exit_button` / `exit_hold` в `module.json`.

## Память

`malloc` нет. Программа получает пул (около 16 КБ), он обнуляется перед запуском
и возвращается системе после выхода.

```c
void *mem_alloc(int n);      /* выровнено на 4, обнулено, 0 если не влезло */
int   mem_left(void);
int   mem_biggest(void);
```

Статические переменные модуля живут в RTC-памяти — её около семи килобайт на все
программы, так что большие массивы берите `mem_alloc()`. Изменяемые данные с
начальным значением упаковщик не пропустит: копировать их в ОЗУ некому.

## Рекорды и настройки

```c
uint32_t hi_get(int slot);               /* слот 0 или 1, свой у каждой программы */
void     hi_set(int slot, uint32_t v);   /* запишет, только если больше */

enum { CFG_SND, CFG_MUTE, CFG_INIT, CFG_N };
int  cfg_get(int id);
void cfg_set(int id, int v);
```

Рекорды переживают выключение: ядро уносит их в сектор флеша.

## Время и прочее

```c
uint32_t now_ms(void);
void     plat_sleep_ms(uint32_t ms);
void     plat_yield(void);
uint32_t plat_rand(void);
void     plat_srand(uint32_t seed);
uint32_t plat_ticks(void);          /* счётчик тиков прошивки, 5 мс */
uint32_t plat_cycles(void);
uint32_t plat_cyc_per_ms(void);
void     plat_exit_to_stock(void);  /* выйти из мода (это перезагрузка) */
void     plat_log(const char *fmt, ...);
```

## Математика и строки

64-битного деления нет (libgcc не линкуется), `printf` нет.

```c
#define FX_TURN 1024      /* полный оборот */
#define FX_ONE  16384     /* 1.0 для fx_sin/fx_cos */
int fx_sin(int a);        int fx_cos(int a);
int fx_sin_hi(int a);     /* 4096 единиц на оборот */
int fx_sqrt(int v);       int fx_hypot(int dx, int dy);
int fx_atan2(int y, int x);
int fx_fmt(char *out, int cap, const char *fmt, ...);   /* %d %s %c %x %% и %02d */

int iabs(int v); int imin(int a, int b); int imax(int a, int b);
int iclamp(int v, int lo, int hi);
void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);
void *memmove(void *d, const void *s, size_t n);
size_t strlen(const char *s); int strcmp(const char *a, const char *b);
char *strcpy(char *d, const char *s);
```

## Звук

Одна ножка, меандр. Эффекты стоят в очереди и играются по ноте за кадр из
`game_frame_wait()`, чтобы звук не рвал картинку.

```c
typedef struct { uint16_t hz; uint16_t ms; } snote;
#define SND(x)  snd_play(x, sizeof(x) / sizeof(snote))
void snd_play(const snote *seq, int n);
void snd_music(const snote *seq, int n);
void snd_music_stop(void);
void snd_tone(int hz, int ms);
int  snd_enabled(void);
extern const snote snd_shot[3], snd_hurt[2], snd_pick[2], snd_door[2],
                   snd_click[1], snd_die[4], snd_win[3], snd_hit[2];
```

## Готовые экраны

```c
void game_banner(const band *b, const char *big, const char *small, px tint);
int  game_confirm(const char *text);   /* ◀ нет, ▶ да */
```

## Свои данные

Большие куски держите файлами и описывайте в `module.json`:

```json
"blobs": { "level_data": "data/levels.bin" }
```

```c
extern const uint8_t level_data[];
```

Studio положит файл в образ и подставит адрес при установке.

## Что проверяет `zealmod check`

Модуль не пройдёт, если он просит ABI новее, чем у ядра; ссылается на имя,
которого ядро не отдаёт; имеет изменяемую секцию с данными; не имеет точки
входа. Всё остальное — ОЗУ, глубина стека, время кадра — на вашей совести.
