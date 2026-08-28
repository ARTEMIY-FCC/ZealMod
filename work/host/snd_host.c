/* на стенде звука нет — просто заглушки */
#include "../src/snd.h"
static int p, on, mut;
const snote snd_shot[3]  = { { 820, 40 }, { 480, 40 }, { 240, 60 } };
const snote snd_hurt[2]  = { { 190, 10 }, { 140, 10 } };
const snote snd_pick[2]  = { { 880, 6 }, { 1240, 7 } };
const snote snd_door[2]  = { { 130, 11 }, { 160, 11 } };
const snote snd_click[1] = { { 1150, 4 } };
const snote snd_die[4]   = { { 300, 11 }, { 240, 11 }, { 180, 11 }, { 120, 12 } };
const snote snd_win[3]   = { { 659, 9 }, { 784, 9 }, { 988, 12 } };
const snote snd_hit[2]   = { { 620, 5 }, { 380, 6 } };
void snd_pin(int g) { p = g; }
int  snd_get_pin(void) { return p; }
void snd_tone(int hz, int ms) { (void)hz; (void)ms; }
void snd_off(void) { }
void snd_mute_stock(int o) { mut = o; }
int  snd_stock_muted(void) { return mut; }
void snd_play(const snote *s, int n) { (void)s; (void)n; }
void snd_update(void) { }
void snd_enable(int o) { on = o; }
void snd_music(const snote *s, int n) { (void)s; (void)n; }
void snd_music_stop(void) { }
void snd_probe(int g, int hz, int ms) { (void)g; (void)hz; (void)ms; }
int  snd_music_playing(void) { return 0; }
int  snd_enabled(void) { return on; }
