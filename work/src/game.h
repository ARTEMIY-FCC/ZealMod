/* game.h — то, что общего у всех игр. */
#ifndef GAME_H
#define GAME_H
#include "plat.h"
#include "fx.h"

/* Кнопки приходят по одной (прошивка не умеет иначе), поэтому выход — долгое
 * нажатие одной кнопки.  Игра сама говорит, какой: той, которую она не держит
 * подолгу по ходу дела. */
#define EXIT_HOLD_MS 1400

void game_exit_button(int btn);        /* по умолчанию «вверх» */
void game_exit_hold(int ms);           /* по умолчанию EXIT_HOLD_MS */
void game_exit_defaults(void);         /* меню зовёт перед запуском игры */
int  game_quit(void);                  /* 1 = пора возвращаться в меню */
void game_frame_wait(uint32_t *last, int ms);
void game_banner(const band *b, const char *big, const char *small, px tint);
int  game_confirm(const char *text);   /* да/нет: LEFT = нет, RIGHT = да */

/* рекорды переживают мягкую перезагрузку (RTC-память) */
/* Слоты рекордов свои у каждого модуля: меню перед запуском говорит, чьи они
 * теперь, поэтому игра пишет просто hi_set(0, ...) и ни с кем не сталкивается. */
#define HI_PER_MOD 2
#define HI_N       (32 * HI_PER_MOD)
uint32_t hi_get(int slot);
void     hi_put(int slot, uint32_t v);   /* абсолютный слот: восстановление из флеша */
void     hi_set(int slot, uint32_t v);
uint32_t hi_raw(int slot);               /* абсолютный слот: сохранение во флеш */
void     zm_set_slot(int mod);           /* зовёт меню перед запуском модуля */
#endif
