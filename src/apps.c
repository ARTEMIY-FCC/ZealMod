/* apps.c — встроенный список приложений.
 *
 * Так собирается образ разработчика (`make`): всё внутри ядра.  Studio же
 * собирает ядро с -DZM_NO_BUILTINS, а приложения ставит модулями, поэтому
 * здесь остаётся пустой список.
 */
#include "plat.h"

#ifdef ZM_NO_BUILTINS
const app_t *const apps[] = { 0 };
const int apps_n = 0;
#else

#define COVER(n) extern const uint8_t cover_##n[]; extern const px cover_##n##_pal[];
COVER(doom) COVER(mine) COVER(pong) COVER(g2048) COVER(snake) COVER(tetris)
COVER(maze) COVER(maze3d) COVER(gravity) COVER(clock) COVER(calendar) COVER(diag) COVER(sound)

void run_doom(void); void run_mine(void); void run_pong(void); void run_2048(void); void run_snake(void);
void run_tetris(void); void run_maze(void); void run_maze3d(void); void run_gravity(void);
void run_clock(void); void run_calendar(void); void run_diag(void); void run_sound(void);

#define APP(id, name, cov, fn) \
    static const app_t app_##id = { name, cover_##cov, cover_##cov##_pal, fn };
APP(doom,     "DOOM",       doom,     run_doom)
APP(mine,     "Майнкрафт",  mine,     run_mine)
APP(pong,     "Pong",       pong,     run_pong)
APP(g2048,    "2048",       g2048,    run_2048)
APP(snake,    "Змейка",     snake,    run_snake)
APP(tetris,   "Тетрис",     tetris,   run_tetris)
APP(maze,     "Лабиринт",   maze,     run_maze)
APP(maze3d,   "Лабиринт 3D", maze3d,  run_maze3d)
APP(gravity,  "Gravity",    gravity,  run_gravity)
APP(clock,    "Часы",       clock,    run_clock)
APP(calendar, "Календарь",  calendar, run_calendar)
APP(diag,     "Настройка",  diag,     run_diag)
APP(sound,    "Звук",       sound,    run_sound)

const app_t *const apps[] = {
    &app_g2048, &app_snake, &app_tetris, &app_pong, &app_maze,
    &app_maze3d, &app_gravity, &app_mine, &app_doom, &app_clock, &app_calendar, &app_diag, &app_sound,
};
const int apps_n = (int)(sizeof apps / sizeof *apps);
#endif
