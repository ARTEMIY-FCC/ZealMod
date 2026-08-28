/* nvram.c — то, что должно пережить выключение питания.
 *
 * RTC-память держится только до снятия питания, поэтому часы, рекорды и
 * настройки кладём в последний сектор раздела ota_0: он за концом образа и
 * прошивке не нужен.  Пишем редко — при сохранении времени, новом рекорде и
 * при выходе из мода. */
#include "plat.h"
#include "fx.h"
#include "zeal_fw.h"
#include "nvram.h"

#define NV_MAGIC 0x324E4D5Au      /* 'ZMN2' — формат ZealMod */

nvram_t nv;

static uint32_t nv_sum(const nvram_t *n)
{
    const uint8_t *p = (const uint8_t *)n;
    uint32_t s = 0x1234;
    for (unsigned i = 0; i < sizeof *n - 4; i++) s = s * 31u + p[i];
    return s;
}

/* На время работы с флешем кэш выключается — и всё, что лежит во флеше,
 * перестаёт существовать.  У прошивки колбэк SPI (0x42005C04) как раз оттуда,
 * и стоит ему сработать во время стирания, система падает с «illegal
 * instruction».  Прерывания глушить нельзя (драйвер их ждёт и ловит сторожа),
 * поэтому на время операции подменяем сам колбэк заглушкой, живущей в
 * RTC-памяти: она не во флеше, и вызывать её безопасно. */
#define BAD_CB 0x42005C04u
#define STUB_AT 0x50001F80u          /* хвост RTC-памяти, за нашими данными */
#define MAX_SLOTS 6

static uint32_t *slots[MAX_SLOTS];
static int nslots, slots_ready;

static void stub_build(void)
{
    volatile uint32_t *p = (volatile uint32_t *)STUB_AT;
    p[0] = 0x00000513u;              /* li a0, 0 */
    p[1] = 0x00008067u;              /* ret       */
}

static void slots_find(void)
{
    nslots = 0;
    for (uint32_t *p = (uint32_t *)0x3FC8E200u; p < (uint32_t *)0x3FCDF000u; p++) {
        if (*p != BAD_CB) continue;
        if (nslots < MAX_SLOTS) slots[nslots++] = p;
    }
    stub_build();
}

static void cb_swap(int to_stub)
{
    if (!slots_ready) { slots_find(); slots_ready = 1; }
    for (int i = 0; i < nslots; i++) *slots[i] = to_stub ? STUB_AT : BAD_CB;
}

void nv_load(void)
{
    nvram_t tmp;
    cb_swap(1);
    int r = fw_flash_read(NVRAM_ADDR, &tmp, sizeof tmp);
    cb_swap(0);
    if (r != 0) return;
    if (tmp.magic != NV_MAGIC || tmp.sum != nv_sum(&tmp)) return;
    nv = tmp;
    extern int nv_valid_flag;
    nv_valid_flag = 1;
}
int nv_valid_flag;

int nv_save(void)
{
    /* Драйверу флеша нужен буфер в обычном ОЗУ, а `nv` живёт в RTC-памяти —
     * запись прямо оттуда роняет систему (store access fault). */
    nvram_t tmp;
    nv.magic = NV_MAGIC;
    nv.sum = nv_sum(&nv);
    tmp = nv;
    cb_swap(1);
    int e = fw_flash_erase(NVRAM_ADDR, 4096);
    int w = e ? -1 : fw_flash_write(NVRAM_ADDR, &tmp, sizeof tmp);
    cb_swap(0);
    return e == 0 && w == 0;
}

/* ---- связка с оперативной памятью --------------------------------------- */
#include "game.h"
#include "apps/wallclock.h"

extern uint32_t zg_cfg_stamp;
static int dirty, pending;
int  nv_pending(void) { return pending; }
void nv_set_pending(int v) { pending = v; }
#define valid nv_valid_flag
extern int nv_valid_flag;
static uint32_t last_write;

int nv_valid(void) { return valid; }
void nv_dirty(void) { dirty = 1; }

void nv_sync_from_ram(void)
{
    nv.epoch_sec = wc_epoch();
    for (int i = 0; i < HI_N && i < 64; i++) nv.hi[i] = hi_raw(i);
    for (int i = 0; i < CFG_N && i < 4; i++) nv.cfg[i] = (uint8_t)cfg_get(i);
    for (int i = 0; i < BTN_N && i < 4; i++) nv.btn[i] = (uint8_t)in_get_map(i);
    nv.stamp = zg_cfg_stamp;
    nv.saves++;
}

void nv_restore_to_ram(void)
{
    if (!valid) return;
    for (int i = 0; i < HI_N && i < 64; i++) hi_put(i, nv.hi[i]);
    for (int i = 0; i < CFG_N && i < 4; i++) cfg_set(i, nv.cfg[i]);
    for (int i = 0; i < BTN_N && i < 4; i++) in_set_map(i, nv.btn[i]);
    zg_cfg_stamp = nv.stamp;
    wc_set_epoch(nv.epoch_sec);
}

/* Флеш живёт около 100 тысяч стираний на сектор, поэтому пишем скупо: если
 * что-то поменялось — не чаще раза в минуту, а просто ход часов сохраняем раз
 * в десять минут.  Стирание занимает десятки миллисекунд, так что зовём это
 * только из холостого цикла, когда меню закрыто. */
void nv_flush(void)
{
    uint32_t age = last_write ? now_ms() - last_write : 0xFFFFFFFFu;
    if (!((dirty && age > 60000u) || age > 600000u)) return;
    nv_sync_from_ram();
    if (nv_save()) { dirty = 0; last_write = now_ms() ? now_ms() : 1; }
}


/* ---- единственное безопасное окно: самое начало загрузки ----------------
 * Здесь прошивка ещё не настроила SPI и прерывания, поэтому флеш слушается.
 * Читаем сохранённое и, если RTC-память пережила мягкий сброс, тут же
 * записываем её обратно — значит выход из мода (а он через перезагрузку)
 * заодно сохраняет часы, рекорды и настройки.
 */
void nv_boot_io(void)
{
    nv_load();
    if (nv_pending()) {              /* RTC цела: сохраняем то, что в ней */
        nv_sync_from_ram();
        nv_save();
    } else if (nv_valid()) {
        nv_restore_to_ram();         /* было выключение питания: берём из флеша */
    }
}
