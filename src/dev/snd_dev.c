/* snd_dev.c — звук аппаратным ШИМ (LEDC).
 *
 * Раньше меандр дрыгался вручную по GPIO_OUT, но на ту же ножку 21 пишет и
 * прошивка (данные для внешнего дисплея) — от наложения звук превращался в
 * треск.  Теперь ножка через матрицу подцеплена прямо к выходу LEDC: записи
 * прошивки до контакта не доходят вовсе, а тон формируется железом, не занимая
 * процессор.  Номер сигнала подсмотрен у самой прошивки: у неё подсветка на
 * ножке 8 разведена сигналом 45 — это канал 0, значит канал N = 45 + N. */
#include "snd.h"
#include "plat.h"
#include "zeal_fw.h"

#define REG(a) (*(volatile uint32_t *)(a))
#define GPIO_OUT_W1TC    0x6000400Cu
#define GPIO_ENABLE_W1TS 0x60004024u
#define GPIO_ENABLE_W1TC 0x60004028u
#define GPIO_FUNC_OUT(n) (0x60004554u + 4u * (n))
#define IO_MUX_GPIO(n)   (0x60009004u + 4u * (n))
#define IOMUX_FUN_PD     (1u << 7)
#define SIG_GPIO_OUT     128

#define LEDC_BASE        0x60019000u
#define LEDC_CH_CONF0(n)  (LEDC_BASE + 0x0000u + (n) * 0x14u)
#define LEDC_CH_HPOINT(n) (LEDC_BASE + 0x0004u + (n) * 0x14u)
#define LEDC_CH_DUTY(n)   (LEDC_BASE + 0x0008u + (n) * 0x14u)
#define LEDC_CH_CONF1(n)  (LEDC_BASE + 0x000Cu + (n) * 0x14u)
#define LEDC_TIMER_CONF(n) (LEDC_BASE + 0x00A0u + (n) * 8u)
#define SIG_LEDC0        45
#define LEDC_CONF_REG     (LEDC_BASE + 0x00D0u)
#define SND_CH           2            /* канал 0 занят подсветкой */
#define SND_TIMER        2
#define DUTY_RES         10           /* 1024 шага */

static int snd_gpio, muted_stock, sound_on;
static uint32_t saved_sel, have_saved;

/* ---- железо ------------------------------------------------------------- */
/* Тактирование LEDC трогать нельзя: у C3 карта системных регистров не такая,
 * как у ESP32, и запись «по адресам от ESP32» портит делитель часов BT — плата
 * уходит в вечную перезагрузку.  Прошивка и так держит LEDC включённым: на нём
 * висит подсветка экрана, поэтому нам достаточно своих канала и таймера. */
static void ledc_init(void)
{
    static int done;
    if (done) return;
    done = 1;
    REG(LEDC_CONF_REG) = (REG(LEDC_CONF_REG) & ~3u) | 1u;   /* источник — APB */
}

static void ledc_freq(int hz)
{
    ledc_init();
    if (hz < 90) hz = 90;
    if (hz > 6000) hz = 6000;
    uint32_t div = ((80000000u / (uint32_t)hz) << 8) >> DUTY_RES;   /* APB 80 МГц, Q8 */
    if (div < 1) div = 1;
    if (div > 0x3FFFF) div = 0x3FFFF;
    REG(LEDC_TIMER_CONF(SND_TIMER)) = (DUTY_RES & 0xF) | (div << 4) | (1u << 24) | (1u << 25);
    REG(LEDC_CH_HPOINT(SND_CH)) = 0;
    REG(LEDC_CH_DUTY(SND_CH)) = (1u << (DUTY_RES - 1)) << 4;        /* меандр 50 % */
    /* низкоскоростным каналам скважность защёлкивается битом para_up в CONF0 */
    REG(LEDC_CH_CONF0(SND_CH)) = (SND_TIMER & 3u) | (1u << 2) | (1u << 4);
}

static void ledc_off(void)
{
    ledc_init();
    REG(LEDC_CH_DUTY(SND_CH)) = 0;
    REG(LEDC_CH_CONF0(SND_CH)) = (SND_TIMER & 3u) | (1u << 4);      /* выход выключен */
}

/* ---- ножка -------------------------------------------------------------- */
void snd_pin(int gpio)
{
    snd_gpio = gpio;
    if (!gpio) return;
    if (!have_saved) { saved_sel = REG(GPIO_FUNC_OUT(gpio)); have_saved = 1; }
    REG(GPIO_FUNC_OUT(gpio)) = SIG_LEDC0 + SND_CH;   /* ножку ведёт LEDC, а не прошивка */
    REG(GPIO_ENABLE_W1TS) = 1u << gpio;
    ledc_off();
}

int snd_get_pin(void) { return snd_gpio; }

void snd_mute_stock(int on)
{
    muted_stock = on;
    if (!snd_gpio) return;
    if (on) {
        snd_pin(snd_gpio);                            /* через LEDC = данные не проходят */
    } else {
        REG(GPIO_FUNC_OUT(snd_gpio)) = have_saved ? saved_sel : SIG_GPIO_OUT;
        REG(GPIO_ENABLE_W1TS) = 1u << snd_gpio;       /* вернуть вывод прошивке */
        REG(IO_MUX_GPIO(snd_gpio)) &= ~IOMUX_FUN_PD;
    }
}

int snd_stock_muted(void) { return muted_stock; }

void snd_off(void) { ledc_off(); }

/* ---- ноты --------------------------------------------------------------- */
static uint32_t note_until;

static void note_start(int hz, int ms)
{
    if (!sound_on || !snd_gpio) return;
    if (hz <= 0) { ledc_off(); note_until = now_ms() + (uint32_t)ms; return; }
    if (!muted_stock) {                                /* звук и без глушения */
        REG(GPIO_FUNC_OUT(snd_gpio)) = SIG_LEDC0 + SND_CH;
        REG(GPIO_ENABLE_W1TS) = 1u << snd_gpio;
    }
    ledc_freq(hz);
    note_until = now_ms() + (uint32_t)ms;
}

void snd_tone(int hz, int ms)          /* блокирующий — нужен только для проверки */
{
    note_start(hz, ms);
    plat_sleep_ms((uint32_t)ms);
    ledc_off();
    note_until = 0;
}

/* ---- очередь эффектов и музыка ------------------------------------------ */
#define QMAX 16
static snote queue[QMAX];
static int qhead, qtail;

const snote snd_shot[3]  = { { 820, 40 }, { 480, 40 }, { 240, 60 } };
const snote snd_hurt[2]  = { { 190, 70 }, { 130, 90 } };
const snote snd_pick[2]  = { { 880, 45 }, { 1320, 60 } };
const snote snd_door[2]  = { { 130, 90 }, { 165, 120 } };
const snote snd_click[1] = { { 1150, 22 } };
const snote snd_die[4]   = { { 300, 90 }, { 240, 90 }, { 180, 110 }, { 110, 200 } };
const snote snd_win[3]   = { { 659, 80 }, { 784, 80 }, { 988, 150 } };
const snote snd_hit[2]   = { { 640, 30 }, { 360, 40 } };

void snd_enable(int on)
{
    sound_on = on;
    if (!on) { qhead = qtail = 0; note_until = 0; ledc_off(); }
}

int snd_enabled(void) { return sound_on; }

void snd_play(const snote *seq, int n)
{
    if (!sound_on) return;
    for (int i = 0; i < n; i++) {
        int nx = (qtail + 1) % QMAX;
        if (nx == qhead) return;
        queue[qtail] = seq[i];
        qtail = nx;
    }
}

/* --- фоновая мелодия ----------------------------------------------------- */
static const snote *tune;
static int tune_len, tune_pos, tune_on;

void snd_music(const snote *seq, int n)
{
    tune = seq; tune_len = n; tune_pos = 0; tune_on = seq && n > 0;
}

void snd_music_stop(void) { tune_on = 0; }
int  snd_music_playing(void) { return tune_on; }

/* Зовётся раз в кадр.  Ничего не ждёт: тон держит железо, мы только меняем
 * ноты по времени.  Эффекты важнее музыки. */
void snd_update(void)
{
    if (!sound_on) return;
    if (note_until && (int32_t)(now_ms() - note_until) < 0) return;   /* нота ещё звучит */
    if (qhead != qtail) {
        snote n = queue[qhead];
        qhead = (qhead + 1) % QMAX;
        note_start(n.hz, n.ms);
        return;
    }
    if (tune_on && tune_len) {
        snote n = tune[tune_pos];
        tune_pos = (tune_pos + 1) % tune_len;
        note_start(n.hz, n.ms);
        return;
    }
    if (note_until) { ledc_off(); note_until = 0; }
}


/* Проба другой ножки: вдруг правый канал разъёма тоже куда-то заведён. */
void snd_probe(int gpio, int hz, int ms)
{
    int keep = snd_gpio;
    uint32_t keep_sel = saved_sel, keep_have = have_saved;
    have_saved = 0;
    snd_pin(gpio);
    snd_tone(hz, ms);
    REG(GPIO_FUNC_OUT(gpio)) = have_saved ? saved_sel : SIG_GPIO_OUT;
    saved_sel = keep_sel; have_saved = keep_have;
    snd_gpio = keep;
    if (muted_stock) snd_pin(keep);
}
