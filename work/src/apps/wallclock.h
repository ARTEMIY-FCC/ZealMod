#ifndef WALLCLOCK_H
#define WALLCLOCK_H
#include <stdint.h>
typedef struct { int year, mon, day, hour, min, sec, wday, valid; } wc_time;
wc_time  wc_now(void);
void     wc_set(int y, int mo, int d, int h, int mi);
int      wc_valid(void);
int      wc_setup(void);
void     wc_tick(void);
void     wc_boot(void);
uint32_t wc_epoch(void);
void     wc_set_epoch(uint32_t sec);
uint32_t wc_days_from_civil(int y, int m, int d);
void     wc_civil_from_days(uint32_t days, int *y, int *m, int *d);
/* названия на языке образа (см. TR в plat.h) */
const char *wc_month(int m);        /* «январь» / "January" */
const char *wc_month_gen(int m);    /* «января» — для даты */
const char *wc_wday(int d);         /* «пн» / "Mon" */
#endif
