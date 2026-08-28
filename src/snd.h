/* snd.h — звук: пока это меандр на ножке, найденной щупом «Звук». */
#ifndef SND_H
#define SND_H
#include <stdint.h>

#define SND_GPIO 21                  /* найдено на живом таймере: ножка разъёма */

typedef struct { uint16_t hz; uint16_t ms; } snote;

void snd_pin(int gpio);              /* какую ножку дёргать (0 = молчать) */
int  snd_get_pin(void);
void snd_tone(int hz, int ms);       /* блокирующий тон */
void snd_off(void);
void snd_mute_stock(int on);         /* забрать ножку у прошивки: её сигнал трещит в ухе */
int  snd_stock_muted(void);

/* Очередь: игра ставит эффект, а snd_update() играет по одной ноте за кадр —
 * так звук не рвёт картинку. */
void snd_play(const snote *seq, int n);
void snd_update(void);
void snd_enable(int on);
int  snd_enabled(void);

/* фоновая мелодия: список нот, играется по кругу, эффекты её перебивают */
void snd_music(const snote *seq, int n);
void snd_music_stop(void);
void snd_probe(int gpio, int hz, int ms);   /* сыграть на другой ножке и вернуться */
int  snd_music_playing(void);

#define SND_SEQ(name, ...) static const snote name[] = { __VA_ARGS__ }
extern const snote snd_shot[3], snd_hurt[2], snd_pick[2], snd_door[2],
                   snd_click[1], snd_die[4], snd_win[3], snd_hit[2];
#define SND(x) snd_play(x, (int)(sizeof(x) / sizeof(snote)))
#endif
