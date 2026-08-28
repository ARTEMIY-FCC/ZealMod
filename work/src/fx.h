/* fx.h — целочисленная математика: угол 1/1024 оборота, синус в Q14. */
#ifndef FX_H
#define FX_H
#include <stdint.h>
#include <stddef.h>

#define FX_TURN 1024
#define FX_ONE  16384           /* 1.0 для fx_sin/fx_cos */

int fx_sin(int a);
int fx_sin_hi(int a);   /* 4096 единиц на оборот, Q14 */
int fx_cos(int a);
int fx_sqrt(int v);
int fx_atan2(int y, int x);
int fx_hypot(int dx, int dy);
int fx_fmt(char *out, int cap, const char *fmt, ...);
int fx_vfmt(char *out, int cap, const char *fmt, __builtin_va_list ap);

#ifdef PLAT_HOST
#include <string.h>
#else
void  *memset(void *d, int c, size_t n);
void  *memcpy(void *d, const void *s, size_t n);
void  *memmove(void *d, const void *s, size_t n);
size_t strlen(const char *s);
int    strcmp(const char *a, const char *b);
char  *strcpy(char *d, const char *s);
#endif

static inline int iabs(int v) { return v < 0 ? -v : v; }
static inline int imin(int a, int b) { return a < b ? a : b; }
static inline int imax(int a, int b) { return a > b ? a : b; }
static inline int iclamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
#endif
