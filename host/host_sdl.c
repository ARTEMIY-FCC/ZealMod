/* host_sdl.c — тот же код игр, но на маке: окно, клавиши, снимки экрана.
 *
 *   ./build/zgames                       — играть
 *   ./build/zgames --script "120.,8R,60." --shots 1,60,190 --out build/shot
 *                                        — прогнать без окна и сохранить кадры
 */
#define PLAT_HOST 1
#include "../src/plat.h"
#include "../src/fx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

static px fb[SCR_W * SCR_H];
int g_band_h = BAND_H;
static uint8_t cfg_[CFG_N];
int  cfg_get(int id) { return (unsigned)id < CFG_N ? cfg_[id] : 0; }
void cfg_set(int id, int v) { if ((unsigned)id < CFG_N) cfg_[id] = (uint8_t)v; }   /* --bands N — прогнать код в режиме узких полос */
static band cur;
static int started;
static uint32_t held, hit, rep_mask, hold_since[BTN_N], rep_at[BTN_N];
static uint32_t vtime;          /* виртуальные мс в headless-режиме */
static int headless, frame_no, want_exit;
static const char *out_prefix = "build/shot";
static int shots[64], n_shots;

/* ---- сценарий ввода ---------------------------------------------------- */
static struct { int frames; uint32_t mask; } script[256];
static int n_script, script_i, script_left;

static uint32_t mask_of(const char *s, int n)
{
    uint32_t m = 0;
    for (int i = 0; i < n; i++)
        switch (s[i]) {
        case 'U': m |= B_UP; break;
        case 'D': m |= B_DOWN; break;
        case 'L': m |= B_LEFT; break;
        case 'R': m |= B_RIGHT; break;
        }
    return m;
}

static void parse_script(const char *s)
{
    while (*s && n_script < 256) {
        int n = 0;
        while (*s >= '0' && *s <= '9') n = n * 10 + (*s++ - '0');
        const char *k = s;
        while (*s && *s != ',') s++;
        script[n_script].frames = n ? n : 1;
        script[n_script].mask = mask_of(k, (int)(s - k));
        n_script++;
        if (*s) s++;
    }
    script_left = n_script ? script[0].frames : 0;
}

static uint32_t script_mask(void)
{
    if (script_i >= n_script) return 0;
    if (script_left-- <= 0) {
        if (++script_i >= n_script) return 0;
        script_left = script[script_i].frames - 1;
    }
    return script[script_i].mask;
}

/* ---- SDL --------------------------------------------------------------- */
#ifndef NO_SDL
#include <SDL.h>
static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *tex;
#endif

static void save_ppm(void)
{
    char path[256];
    snprintf(path, sizeof path, "%s%03d.ppm", out_prefix, frame_no);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", SCR_W, SCR_H);
    for (int i = 0; i < SCR_W * SCR_H; i++) {
        int r, g, b;
        px_unpack(fb[i], &r, &g, &b);
        fputc(r, f); fputc(g, f); fputc(b, f);
    }
    fclose(f);
    fprintf(stderr, "кадр %d -> %s\n", frame_no, path);
}

static void present(void)
{
    frame_no++;
    for (int i = 0; i < n_shots; i++)
        if (shots[i] == frame_no) save_ppm();
    if (headless) {
        vtime += 33;
        if (n_shots && frame_no > shots[n_shots - 1]) exit(0);
        if (frame_no > 100000) exit(0);
        return;
    }
#ifndef NO_SDL
    uint32_t rgb[SCR_W * SCR_H];
    for (int i = 0; i < SCR_W * SCR_H; i++) {
        int r, g, b;
        px_unpack(fb[i], &r, &g, &b);
        rgb[i] = 0xFF000000u | (r << 16) | (g << 8) | b;
    }
    SDL_UpdateTexture(tex, 0, rgb, SCR_W * 4);
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, 0, 0);
    SDL_RenderPresent(ren);
#endif
}

void fb_rows(int y0, int y1) { (void)y0; (void)y1; }
void fb_begin(void) { cur.p = fb; cur.y0 = 0; cur.y1 = g_band_h; started = 0; }

band *fb_next(void)
{
    if (started) {
        cur.y0 = cur.y1;
        cur.y1 = cur.y0 + g_band_h > SCR_H ? SCR_H : cur.y0 + g_band_h;
        if (cur.y0 >= SCR_H) { present(); return 0; }
        cur.p = fb + cur.y0 * SCR_W;
    }
    started = 1;
    return &cur;
}

uint32_t now_ms(void)
{
    if (headless) return vtime;
#ifndef NO_SDL
    return SDL_GetTicks();
#else
    return vtime;
#endif
}

void plat_sleep_ms(uint32_t ms)
{
    if (headless) { vtime += ms; return; }
#ifndef NO_SDL
    SDL_Delay(ms);
#endif
}

void plat_yield(void) { plat_sleep_ms(1); }
void plat_log(const char *fmt, ...)
{ va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap); fputc('\n', stderr); }

void plat_exit_to_stock(void) { exit(0); }

void in_poll(void)
{
    uint32_t m = 0, now = now_ms();
    if (headless || n_script) m = script_mask();
#ifndef NO_SDL
    if (!headless) {
        SDL_Event e;
        while (SDL_PollEvent(&e))
            if (e.type == SDL_QUIT || (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE))
                exit(0);
        const uint8_t *k = SDL_GetKeyboardState(0);
        if (k[SDL_SCANCODE_UP])    m |= B_UP;
        if (k[SDL_SCANCODE_DOWN])  m |= B_DOWN;
        if (k[SDL_SCANCODE_LEFT])  m |= B_LEFT;
        if (k[SDL_SCANCODE_RIGHT]) m |= B_RIGHT;
    }
#endif
    hit = m & ~held;
    rep_mask = hit;
    for (int i = 0; i < BTN_N; i++) {
        uint32_t bit = 1u << i;
        if (hit & bit) { hold_since[i] = now; rep_at[i] = now + 380; }
        else if (m & bit && (int32_t)(now - rep_at[i]) >= 0) { rep_mask |= bit; rep_at[i] = now + 110; }
    }
    held = m;
}

uint32_t in_held(void) { return held; }
uint32_t in_hit(void) { return hit; }
uint32_t in_rep(void) { return rep_mask; }
uint32_t in_held_ms(int b) { return (held & (1u << b)) ? now_ms() - hold_since[b] : 0; }
void in_set_map(int i, int r) { (void)i; (void)r; }
int  in_get_map(int i) { return i; }
const volatile uint8_t *in_raw(void) { return 0; }
uint32_t plat_cycles(void) { return (uint32_t)clock(); }
uint32_t plat_cyc_per_ms(void) { return CLOCKS_PER_SEC / 1000; }
uint32_t plat_ticks(void) { return now_ms() / 5; }

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--lang") && i + 1 < argc) {   /* язык мода: en или ru */
            extern int zm_lang_force;
            zm_lang_force = !strcmp(argv[++i], "ru") ? 1 : 0;
        }
        else if (!strcmp(argv[i], "--script") && i + 1 < argc) { parse_script(argv[++i]); headless = 1; }
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) out_prefix = argv[++i];
        else if (!strcmp(argv[i], "--bands") && i + 1 < argc) g_band_h = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--shots") && i + 1 < argc) {
            for (char *p = argv[++i]; *p; ) {
                shots[n_shots++] = (int)strtol(p, &p, 10);
                if (*p == ',') p++;
            }
        }
    }
    static uint8_t arena[96 * 1024];
    mem_init(arena, sizeof arena);
    mem_lock();
    plat_srand((uint32_t)time(0));
#ifndef NO_SDL
    if (!headless) {
        SDL_Init(SDL_INIT_VIDEO);
        win = SDL_CreateWindow("Zeal games", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               SCR_W * 2, SCR_H * 2, 0);
        ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                SCR_W, SCR_H);
    }
#endif
    menu_run();
    return 0;
}
