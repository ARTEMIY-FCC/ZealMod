/* util.c — арифметика без FPU и libc: у ESP32-C3 нет ни того, ни другого. */
#include "plat.h"
#include "fx.h"

extern const int16_t  g_sin_q14[257];
extern const uint16_t g_atan_q[257];

/* Оперативка мода собрана из огрызков: кусок кучи прошивки, свой стек, буфер
 * LVGL (пока меню наше, LVGL заморожен и буфер простаивает).  Отсюда — не один
 * массив, а несколько пулов; каждый раздаётся указателем вверх. */
#define MEM_POOLS 4
static struct { uint8_t *base; int size, used, locked; } pool[MEM_POOLS];
static int npool;

void mem_init(void *base, int size) { npool = 0; mem_add(base, size); }

void mem_add(void *base, int size)
{
    if (!base || size < 64 || npool >= MEM_POOLS) return;
    pool[npool].base = base; pool[npool].size = size;
    pool[npool].used = pool[npool].locked = 0;
    npool++;
}

void mem_lock(void) { for (int i = 0; i < npool; i++) pool[i].locked = pool[i].used; }
void mem_reset(void) { for (int i = 0; i < npool; i++) pool[i].used = pool[i].locked; }

int mem_left(void)
{
    int n = 0;
    for (int i = 0; i < npool; i++) n += pool[i].size - pool[i].used;
    return n;
}

int mem_biggest(void)
{
    int n = 0;
    for (int i = 0; i < npool; i++) {
        int f = pool[i].size - pool[i].used;
        if (f > n) n = f;
    }
    return n;
}

void *mem_alloc(int n)
{
    n = (n + 3) & ~3;
    for (int i = 0; i < npool; i++) {
        if (pool[i].used + n > pool[i].size) continue;
        uint8_t *p = pool[i].base + pool[i].used;
        pool[i].used += n;
        for (int k = 0; k < n; k++) p[k] = 0;
        return p;
    }
    return 0;
}

static uint32_t rng_s;

void plat_srand(uint32_t seed) { rng_s = seed ? seed : 0x2545F491u; }

uint32_t plat_rand(void)
{
    if (!rng_s) rng_s = 0x2545F491u;
    rng_s ^= rng_s << 13; rng_s ^= rng_s >> 17; rng_s ^= rng_s << 5;
    return rng_s;
}

int fx_sin(int a)
{
    a &= FX_TURN - 1;
    int q = a >> 8, i = a & 255;
    switch (q) {
    case 0: return  g_sin_q14[i];
    case 1: return  g_sin_q14[256 - i];
    case 2: return -g_sin_q14[i];
    default: return -g_sin_q14[256 - i];
    }
}

int fx_cos(int a) { return fx_sin(a + FX_TURN / 4); }

/* то же, но 4096 делений на оборот: для DOOM 1024 мало — поворот дёргается
 * ровно по столбцу экрана.  Промежуточные значения берём линейно. */
int fx_sin_hi(int a)
{
    a &= 4095;
    int q = a >> 10, i = (a >> 2) & 255, f = a & 3;
    int v0, v1;
    switch (q) {
    case 0: v0 =  g_sin_q14[i];       v1 =  g_sin_q14[i + 1]; break;
    case 1: v0 =  g_sin_q14[256 - i]; v1 =  g_sin_q14[255 - i]; break;
    case 2: v0 = -g_sin_q14[i];       v1 = -g_sin_q14[i + 1]; break;
    default: v0 = -g_sin_q14[256 - i]; v1 = -g_sin_q14[255 - i]; break;
    }
    return v0 + ((v1 - v0) * f >> 2);
}

int fx_sqrt(int v)
{
    if (v <= 0) return 0;
    int r = 0, b = 1 << 30;
    while (b > v) b >>= 2;
    while (b) {
        if (v >= r + b) { v -= r + b; r = (r >> 1) + b; }
        else r >>= 1;
        b >>= 2;
    }
    return r;
}

int fx_atan2(int y, int x)
{
    if (!x && !y) return 0;
    int ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
    while (ax > (1 << 22) || ay > (1 << 22)) { ax >>= 1; ay >>= 1; }   /* без 64-битного деления */
    int a;
    if (ax >= ay) a = g_atan_q[(ay << 8) / (ax ? ax : 1)];
    else          a = FX_TURN / 4 - g_atan_q[(ax << 8) / ay];
    if (x < 0) a = FX_TURN / 2 - a;
    if (y < 0) a = -a;
    return a & (FX_TURN - 1);
}

int fx_hypot(int dx, int dy) { return fx_sqrt(dx * dx + dy * dy); }

/* libc-минимум: gcc сам зовёт memset/memcpy, а libc мы не линкуем */
#ifndef PLAT_HOST
__attribute__((used)) void *memset(void *d, int c, size_t n)
{ uint8_t *p = d; while (n--) *p++ = (uint8_t)c; return d; }

__attribute__((used)) void *memcpy(void *d, const void *s, size_t n)
{ uint8_t *p = d; const uint8_t *q = s; while (n--) *p++ = *q++; return d; }

__attribute__((used)) void *memmove(void *d, const void *s, size_t n)
{
    uint8_t *p = d; const uint8_t *q = s;
    if (p < q) { while (n--) *p++ = *q++; }
    else { p += n; q += n; while (n--) *--p = *--q; }
    return d;
}

__attribute__((used)) size_t strlen(const char *s) { const char *p = s; while (*p) p++; return (size_t)(p - s); }

__attribute__((used)) int strcmp(const char *a, const char *b)
{ while (*a && *a == *b) { a++; b++; } return (uint8_t)*a - (uint8_t)*b; }

__attribute__((used)) char *strcpy(char *d, const char *s) { char *r = d; while ((*d++ = *s++)) ; return r; }
#endif /* !PLAT_HOST */

/* «%d/%u/%s/%c/%x» и «%0Nd» — больше нам не нужно */
int fx_fmt(char *out, int cap, const char *fmt, ...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int n = fx_vfmt(out, cap, fmt, ap);
    __builtin_va_end(ap);
    return n;
}

int fx_vfmt(char *out, int cap, const char *fmt, __builtin_va_list ap)
{
    int n = 0;
    char tmp[12];
#define PUT(c) do { if (n < cap - 1) out[n] = (c); n++; } while (0)
    for (; *fmt; fmt++) {
        if (*fmt != '%') { PUT(*fmt); continue; }
        fmt++;
        int pad = 0, zero = 0;
        if (*fmt == '0') { zero = 1; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') { pad = pad * 10 + (*fmt - '0'); fmt++; }
        switch (*fmt) {
        case 'd': case 'u': {
            long v = *fmt == 'd' ? __builtin_va_arg(ap, int) : (long)__builtin_va_arg(ap, unsigned);
            int neg = v < 0; unsigned long u = neg ? (unsigned long)-v : (unsigned long)v;
            int k = 0;
            do { tmp[k++] = '0' + (u % 10); u /= 10; } while (u);
            if (neg) { if (zero) PUT('-'); else tmp[k++] = '-'; }
            if (zero) { while (k < pad) tmp[k++] = '0'; }
            else { while (k < pad) { PUT(' '); pad--; } }
            while (k) PUT(tmp[--k]);
            break;
        }
        case 'x': {
            unsigned u = __builtin_va_arg(ap, unsigned);
            int k = 0;
            do { tmp[k++] = "0123456789abcdef"[u & 15]; u >>= 4; } while (u);
            while (k < pad) tmp[k++] = zero ? '0' : ' ';
            while (k) PUT(tmp[--k]);
            break;
        }
        case 's': { const char *s = __builtin_va_arg(ap, const char *); while (*s) PUT(*s++); break; }
        case 'c': PUT((char)__builtin_va_arg(ap, int)); break;
        default: PUT(*fmt);
        }
    }
    if (cap > 0) out[n < cap ? n : cap - 1] = 0;
    return n;
#undef PUT
}
