/* сгенерировано zealmod emu */
#include "plat.h"

void run_2048(void);
extern const uint8_t cover_g2048[];
extern const px cover_g2048_pal[];
void run_snake(void);
extern const uint8_t cover_snake[];
extern const px cover_snake_pal[];
void run_tetris(void);
extern const uint8_t cover_tetris[];
extern const px cover_tetris_pal[];
void run_pong(void);
extern const uint8_t cover_pong[];
extern const px cover_pong_pal[];
void run_maze(void);
extern const uint8_t cover_maze[];
extern const px cover_maze_pal[];
void run_maze3d(void);
extern const uint8_t cover_maze3d[];
extern const px cover_maze3d_pal[];
void run_gravity(void);
extern const uint8_t cover_gravity[];
extern const px cover_gravity_pal[];
void run_mine(void);
extern const uint8_t cover_mine[];
extern const px cover_mine_pal[];
void run_doom(void);
extern const uint8_t cover_doom[];
extern const px cover_doom_pal[];
void run_clock(void);
extern const uint8_t cover_clock[];
extern const px cover_clock_pal[];
void run_calendar(void);
extern const uint8_t cover_calendar[];
extern const px cover_calendar_pal[];
void run_diag(void);
extern const uint8_t cover_diag[];
extern const px cover_diag_pal[];
void run_sound(void);
extern const uint8_t cover_sound[];
extern const px cover_sound_pal[];

static const app_t app_g2048 = { "2048", cover_g2048, cover_g2048_pal, run_2048 };
static const app_t app_snake = { "Snake", cover_snake, cover_snake_pal, run_snake };
static const app_t app_tetris = { "Tetris", cover_tetris, cover_tetris_pal, run_tetris };
static const app_t app_pong = { "Pong", cover_pong, cover_pong_pal, run_pong };
static const app_t app_maze = { "Maze", cover_maze, cover_maze_pal, run_maze };
static const app_t app_maze3d = { "Maze 3D", cover_maze3d, cover_maze3d_pal, run_maze3d };
static const app_t app_gravity = { "Gravity", cover_gravity, cover_gravity_pal, run_gravity };
static const app_t app_mine = { "Mine", cover_mine, cover_mine_pal, run_mine };
static const app_t app_doom = { "DOOM", cover_doom, cover_doom_pal, run_doom };
static const app_t app_clock = { "Clock", cover_clock, cover_clock_pal, run_clock };
static const app_t app_calendar = { "Calendar", cover_calendar, cover_calendar_pal, run_calendar };
static const app_t app_diag = { "Diagnostics", cover_diag, cover_diag_pal, run_diag };
static const app_t app_sound = { "Sound", cover_sound, cover_sound_pal, run_sound };

const app_t *const apps[] = {
    &app_g2048,
    &app_snake,
    &app_tetris,
    &app_pong,
    &app_maze,
    &app_maze3d,
    &app_gravity,
    &app_mine,
    &app_doom,
    &app_clock,
    &app_calendar,
    &app_diag,
    &app_sound,
};
const int apps_n = (int)(sizeof apps / sizeof *apps);
