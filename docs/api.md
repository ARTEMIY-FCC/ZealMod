# ZealMod API reference

[Русская версия](ru/api.md)

Everything a program may call. The headers live in `dist/sdk/include`
(the same files as `work/src/*.h`) and are added to the include path by
`zealmod pack` and `zealmod emu` automatically.

```c
#include "plat.h"   /* screen, buttons, memory, time, drawing, text, language */
#include "game.h"   /* leaving a program, frame pacing, high scores, dialogs */
#include "fx.h"     /* integer maths, string formatting, mem*/str* */
#include "snd.h"    /* the buzzer */
```

A module must define one entry point (`zm_main` by default — the name comes
from `entry` in `module.json`) and return from it when `game_quit()` becomes
true.

---

## The frame

The screen is 240×240, 16-bit colour. A frame is drawn into *bands* — on the
current watch the band is the whole screen, but a program must never rely on
that: draw the whole scene inside the loop.

```c
void  fb_begin(void);           /* start a frame */
band *fb_next(void);            /* hand the band over, take the next; NULL ends the frame */
void  fb_rows(int y0, int y1);  /* send only these rows this frame */
```

```c
typedef struct { px *p; int y0, y1; } band;
px *band_row(const band *b, int y);   /* the row, already offset for the band */
```

```c
fb_begin();
for (band *b; (b = fb_next()); ) {
    gfx_clear(b, RGB(10, 12, 20));
    gfx_disc(b, 120, 120, 20, WHITE);
}
game_frame_wait(&last, 33);     /* ~30 fps, and let the system breathe */
```

`game_frame_wait()` is not optional: it paces the frame *and* yields. Without
it the watchdog decides the mod has hung and reboots the watch.

## Colour

```c
typedef uint16_t px;                    /* RGB565, byte-swapped for the panel */
#define RGB(r, g, b)                    /* build a colour from 0..255 components */
#define BLACK, WHITE
px  px_scale(px c, int q);              /* c * q / 256 — dim it */
px  px_mix(px a, px b, int t);          /* t = 0..256 */
void px_unpack(px c, int *r, int *g, int *b);
```

## Drawing

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
```

Images:

```c
/* 16-bit, as is */
void gfx_blit(const band *b, int x, int y, int w, int h, const px *src);
/* 8-bit with a palette; key < 0 means opaque */
void gfx_blit8(const band *b, int x, int y, int w, int h, const uint8_t *src,
               const px *pal, int key);
/* the same, stretched, and optionally dimmed (dimq = 0..256) */
void gfx_blit8_scaled(const band *b, int dx, int dy, int dw, int dh,
                      const uint8_t *src, int sw, int sh, const px *pal,
                      int key, int dimq);
```

## Text

The font atlas is 8-bit anti-aliased coverage, UTF-8, Latin and Cyrillic.

```c
int  gfx_text  (const band *b, int x, int y, const font_t *f, px c, const char *s);
int  gfx_text_c(const band *b, int cx, int y, const font_t *f, px c, const char *s);
int  gfx_text_w(const font_t *f, const char *s);         /* width in pixels */
void gfx_text_shadow(const band *b, int x, int y, const font_t *f, px c, px sh,
                     const char *s);

extern const font_t font_s, font_m, font_l, font_xl;
```

`y` is the baseline, not the top.

## Language

The image is built once and carries both languages; which one shows is chosen
in Studio.

```c
enum { ZM_EN = 0, ZM_RU = 1 };
int         zm_lang(void);
const char *zm_tr(const char *en, const char *ru);
#define TR(en, ru)   zm_tr((en), (ru))
#define TRA(en, ru)  /* picks one of two arrays: TRA(en_names, ru_names)[i] */
```

```c
gfx_text_c(b, 120, 40, &font_m, WHITE, TR("Score", "Счёт"));

static const char *label(int i) {
    static const char *const en[3] = { "one", "two", "three" };
    static const char *const ru[3] = { "раз", "два", "три" };
    return TRA(en, ru)[i];
}
```

## Buttons

Four buttons; the stock firmware reports only one at a time, so chords are
impossible — that is why long presses do the work.

```c
enum { BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_N };
#define B_UP, B_DOWN, B_LEFT, B_RIGHT, B_ALL      /* bit masks */

void     in_poll(void);            /* once per frame, before reading anything */
uint32_t in_held(void);            /* mask of buttons held right now */
uint32_t in_hit(void);             /* pressed since the previous poll */
uint32_t in_rep(void);             /* the same, with auto-repeat */
uint32_t in_held_ms(int btn);      /* how long this button has been held */
void     in_set_map(int logical, int raw);
int      in_get_map(int logical);
const volatile uint8_t *in_raw(void);   /* 4 raw bytes; 0 on the desktop */
```

## Leaving a program

```c
void game_exit_button(int btn);    /* which button quits (default ▲) */
void game_exit_hold(int ms);       /* how long to hold it */
int  game_quit(void);              /* 1 = time to return from zm_main() */
```

Pick a button the program does not hold down during play. The defaults come
from `exit_button` / `exit_hold` in `module.json`; calling these overrides them.

## Memory

There is no `malloc`. A program gets a pool (about 16 KB) that is wiped for it
before launch and reclaimed after it returns.

```c
void *mem_alloc(int n);      /* aligned to 4, zeroed, 0 if it does not fit */
int   mem_left(void);        /* how much is left */
int   mem_biggest(void);     /* the largest block available */
```

Static variables of a module live in RTC memory, and there are only about seven
kilobytes of it for all programs together — keep big arrays in `mem_alloc()`.
Initialised mutable data (`static int x = 5;`) is rejected at pack time:
nothing copies it into RAM.

## High scores and settings

```c
uint32_t hi_get(int slot);          /* slot 0 or 1, private to your program */
void     hi_set(int slot, uint32_t v);   /* stores only if bigger */
```

Scores survive a power cut: the core writes them into a flash sector.

```c
enum { CFG_SND, CFG_MUTE, CFG_INIT, CFG_N };
int  cfg_get(int id);
void cfg_set(int id, int v);
```

## Time and the rest

```c
uint32_t now_ms(void);              /* milliseconds since boot */
void     plat_sleep_ms(uint32_t ms);
void     plat_yield(void);
uint32_t plat_rand(void);
void     plat_srand(uint32_t seed);
uint32_t plat_ticks(void);          /* the firmware tick counter, 5 ms */
uint32_t plat_cycles(void);
uint32_t plat_cyc_per_ms(void);
void     plat_exit_to_stock(void);  /* leave the mod (this reboots the watch) */
void     plat_log(const char *fmt, ...);   /* to the USB console */
```

## Maths and strings

No 64-bit division (libgcc is not linked) and no `printf`.

```c
#define FX_TURN 1024      /* a full turn */
#define FX_ONE  16384     /* 1.0 for fx_sin/fx_cos */
int fx_sin(int a);        int fx_cos(int a);
int fx_sin_hi(int a);     /* 4096 units per turn */
int fx_sqrt(int v);       int fx_hypot(int dx, int dy);
int fx_atan2(int y, int x);
int fx_fmt(char *out, int cap, const char *fmt, ...);   /* %d %s %c %x %% and %02d */

int iabs(int v); int imin(int a, int b); int imax(int a, int b);
int iclamp(int v, int lo, int hi);

void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);
void *memmove(void *d, const void *s, size_t n);
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
char *strcpy(char *d, const char *s);
```

## Sound

One pin, one square wave. Effects are queued and played one note per frame by
`game_frame_wait()`, so sound never tears the picture.

```c
typedef struct { uint16_t hz; uint16_t ms; } snote;

#define SND(x)  snd_play(x, sizeof(x) / sizeof(snote))
void snd_play(const snote *seq, int n);
void snd_music(const snote *seq, int n);      /* looping background tune */
void snd_music_stop(void);
void snd_tone(int hz, int ms);                /* blocking */
int  snd_enabled(void);

extern const snote snd_shot[3], snd_hurt[2], snd_pick[2], snd_door[2],
                   snd_click[1], snd_die[4], snd_win[3], snd_hit[2];
```

```c
SND(snd_click);
static const snote fanfare[] = { {660, 80}, {880, 80}, {1320, 160} };
snd_play(fanfare, 3);
```

## Ready-made screens

```c
void game_banner(const band *b, const char *big, const char *small, px tint);
int  game_confirm(const char *text);   /* ◀ no, ▶ yes */
```

## Data files

Big blobs do not belong in C arrays. Put them next to the sources and declare
them in `module.json`:

```json
"blobs": { "level_data": "data/levels.bin" }
```

```c
extern const uint8_t level_data[];
```

Studio places the file in the image and resolves the symbol at install time.

## What the checker enforces

`zealmod check` (and Studio when you drop a `.zm` in) refuses a module that

* asks for a newer ABI than the core provides;
* references a name the core does not export;
* has a writable section with initialised data;
* has no entry function.

Everything else — RAM, stack depth, frame time — is on you.
