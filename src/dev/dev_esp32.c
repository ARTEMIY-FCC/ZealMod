/* dev_esp32.c — платформа «на железе»: два хука в прошивку, своя задача,
 * прямой вывод в ST7789 мимо LVGL и чтение кнопок из памяти прошивки. */
#include "plat.h"
#include "fx.h"
#include "zeal_fw.h"
#include "layout.h"
#include "apps/wallclock.h"
#include "snd.h"
#include "nvram.h"
#include "zmtab.h"

/* ---- состояние (.bss живёт в RTC-памяти, см. ld/payload.ld) ------------- */
extern uint32_t __persist_magic[];
__attribute__((used)) volatile uint32_t zg_active;          /* 1 = экран наш, LVGL глушим */
__attribute__((used)) volatile uint32_t zg_mute_keys;       /* 1 = прошивка не получает события кнопок */
static px      *g_band;               /* полоса кадра */
int             g_band_h;
static void    *lv_drv, *lv_buf;      /* то, что удалось отобрать у LVGL */
static uint32_t lv_buf_px;
__attribute__((used)) volatile uint32_t zg_lv_seen;
__attribute__((used)) volatile int zg_lv_frozen;
static int shot_now;
static uint32_t frame_ms, frame_prev, frames_total, push_cyc, frame_cyc, cyc_mark, draw_mark;
#define lv_seen zg_lv_seen
static int stack_bytes;
static band     cur;
static int      started;
static uint32_t held, prev_held, hit, rep_mask;
static uint32_t hold_since[BTN_N], rep_at[BTN_N];
/* раскладка кнопок переживает перезагрузку: выход из мода — это мягкий сброс */
__attribute__((section(".persist"))) static uint8_t btn_map[BTN_N];
__attribute__((section(".persist"))) static uint8_t cfg[CFG_N];
/* Метка настроек из образа: заливка не гасит питание, поэтому и RTC, и NVRAM
 * переживают её и затёрли бы новые настройки старыми.  Сравниваем метки. */
__attribute__((section(".persist"))) uint32_t zg_cfg_stamp;

int  cfg_get(int id) { return (unsigned)id < CFG_N ? cfg[id] : 0; }
void cfg_set(int id, int v)
{
    if ((unsigned)id >= CFG_N) return;
    cfg[id] = (uint8_t)v;
    if (id == CFG_SND) snd_enable(v);
    if (id == CFG_MUTE) snd_mute_stock(v);
}


#define REG(a) (*(volatile uint32_t *)(a))

/* Указатель на «чип» флеша: ищем описатель раздела ota_0 в памяти прошивки
 * (у него по +12 адрес 0x100000, по +16 размер 2 МБ, по +20 метка), берём из
 * него поле flash_chip. */
void *fw_flash_chip(void)
{
    static void *chip;
    if (chip) return chip;
    for (const uint8_t *p = (const uint8_t *)0x3FC8E200u;
         p < (const uint8_t *)0x3FCDF000u; p += 4) {
        if (*(const uint32_t *)(p + 12) != 0x100000u) continue;
        if (*(const uint32_t *)(p + 16) != 0x200000u) continue;
        if (p[20] != 'o' || p[21] != 't' || p[22] != 'a') continue;
        chip = *(void *const *)p;
        return chip;
    }
    chip = (void *)0x3FC8EBF8u;          /* запасной вариант: найденный однажды */
    return chip;
}

/* ---- время ---------------------------------------------------------------
 * mcycle тикает тактами ядра; сколько их в миллисекунде — меряем на старте по
 * тикам FreeRTOS, чтобы не гадать про 160 МГц и не лезть в systimer, который
 * дёргает ещё и esp_timer.
 */
/* Время берём из счётчика тиков FreeRTOS: он лежит в памяти прошивки и растёт
 * ровно, что бы ни делал процессор.  PCCR (0x7E2) для этого не годится — на
 * живом железе он намерил 48 тысяч тактов на миллисекунду при 160 МГц. */
static uint32_t tick_ms = FW_TICK_MS;
static uint32_t cyc_per_ms;

static uint32_t ticks(void) { return *(volatile uint32_t *)FW_XTICKCOUNT; }

static void time_init(void)
{
    uint32_t t0 = ticks(), c0 = fw_cycles();
    fw_vtask_delay(60);
    uint32_t dt = ticks() - t0;
    cyc_per_ms = dt ? (fw_cycles() - c0) / (dt * tick_ms) : 0;
}

/* звать не реже раза в 26 с (при 160 МГц mcycle столько живёт до переполнения) */
uint32_t now_ms(void) { return ticks() * tick_ms; }
uint32_t plat_ticks(void) { return ticks(); }

uint32_t plat_cycles(void) { return fw_cycles(); }
uint32_t plat_cyc_per_ms(void) { return cyc_per_ms; }

uint32_t zg_sleep_cyc, zg_draw_cyc;

void plat_sleep_ms(uint32_t ms)
{
    uint32_t c0 = fw_cycles();
    fw_vtask_delay((ms + tick_ms - 1) / tick_ms);
    zg_sleep_cyc += fw_cycles() - c0;
}
void plat_yield(void) { fw_vtask_delay(1); }
/* ---- лог в USB Serial/JTAG ----------------------------------------------
 * У прошивки консоль молчит, но сам контроллер USB-JTAG в C3 живой и
 * доступен голыми регистрами — символы прошивки не нужны.  Если хост не
 * читает, FIFO переполняется: тогда просто бросаем байты и идём дальше.
 */
#define USJ_EP1        0x60043000u
#define USJ_EP1_CONF   0x60043004u
#define USJ_WR_DONE    (1u << 0)
#define USJ_TX_FREE    (1u << 1)

static void usj_flush(void)
{
    REG(USJ_EP1_CONF) = USJ_WR_DONE;
    for (int i = 0; i < 20000 && !(REG(USJ_EP1_CONF) & USJ_TX_FREE); i++) ;
}

static void usj_putc(char c)
{
    for (int i = 0; i < 20000; i++)
        if (REG(USJ_EP1_CONF) & USJ_TX_FREE) { REG(USJ_EP1) = (uint8_t)c; return; }
}

#define USJ_RX_AVAIL   (1u << 2)

static int usj_getc(void)
{
    if (!(REG(USJ_EP1_CONF) & USJ_RX_AVAIL)) return -1;
    return (int)(REG(USJ_EP1) & 0xFF);
}

/* Возвращает 0, если хост перестал читать: тогда кадр бросаем целиком, иначе
 * задача залипнет в ожидании места в FIFO на минуты. */
static int usj_write(const uint8_t *p, int n)
{
    for (int i = 0; i < n; i++) {
        int k = 0;
        while (!(REG(USJ_EP1_CONF) & USJ_TX_FREE)) {
            if (++k > 30000) { usj_flush(); return 0; }
        }
        REG(USJ_EP1) = p[i];
        if ((i & 63) == 63) usj_flush();
    }
    usj_flush();
    return 1;
}

void plat_log(const char *fmt, ...)
{
    char buf[160];
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int n = fx_vfmt(buf, sizeof buf, fmt, ap);
    __builtin_va_end(ap);
    if (n > (int)sizeof buf - 1) n = sizeof buf - 1;
    for (int i = 0; i < n; i++) {
        usj_putc(buf[i]);
        if ((i & 63) == 63) usj_flush();
    }
    usj_putc('\r');
    usj_putc('\n');
    usj_flush();
}

/* ---- пульт с компьютера ---------------------------------------------------
 * Раз консоль всё равно висит на USB, пусть она и командует: так мод можно
 * гонять по живому железу, не нажимая ничего руками.
 *   m — открыть меню      x — выйти в таймер     0..9,a — запустить пункт
 *   u d l r — «нажать» кнопку на 160 мс          p — прислать кадр экрана
 */
volatile int zg_cmd_open, zg_cmd_app, zg_cmd_exit, zg_shot, zg_cmd_back, zg_verbose;   /* app: 0 = нет, иначе индекс+1 */
static uint32_t virt_keys, virt_until;

static void poll_cmd(void)
{
    int c;
    while ((c = usj_getc()) >= 0) {
        switch (c) {
        case 'm': zg_cmd_open = 1; break;
        case 'x': zg_cmd_exit = 1; break;
        case 'p': zg_shot = 1; break;
        case 't': plat_log("<TICK %d %d>", (int)ticks(), (int)now_ms()); break;
        case 'b': zg_cmd_back = 1; break;
        case 'v': zg_verbose = !zg_verbose; break;
        case 'z':                       /* изобразить снятие питания: стереть метку RTC */
            __persist_magic[0] = 0;
            plat_log("<RTC помечена как чистая, перезагружаюсь>");
            fw_restart();
            break;
        case 'n': nv_save(); plat_log("<NVRAM записано>"); break;
        case 'f': {                     /* ищем описатель раздела ota_0 в памяти */
            const uint8_t *found = 0;
            for (const uint8_t *p = (const uint8_t *)0x3FC8E200u;
                 p < (const uint8_t *)0x3FCDF000u; p += 4) {
                if (*(const uint32_t *)(p + 12) != 0x100000u) continue;
                if (*(const uint32_t *)(p + 16) != 0x200000u) continue;
                if (p[20] != 'o' || p[21] != 't' || p[22] != 'a') continue;
                found = p;
                break;
            }
            if (!found) { plat_log("<PART не нашёлся>"); break; }
            plat_log("<PART %x: chip=%x type=%d sub=%d метка %s>", (unsigned)(uint32_t)found,
                     (unsigned)*(const uint32_t *)found, (int)*(const uint32_t *)(found + 4),
                     (int)*(const uint32_t *)(found + 8), (const char *)(found + 20));
            break;
        }
        case 'h': {                     /* сколько всего можно взять у кучи */
            void *p1 = fw_malloc(1024);
            fw_free(p1);
            void *p2 = fw_malloc(1024);
            plat_log("<HEAP malloc/free: %x %x %s>", (unsigned)(uint32_t)p1,
                     (unsigned)(uint32_t)p2, p1 == p2 ? "free работает" : "free НЕ тот!");
            fw_free(p2);
            int big = 0;
            for (int sz = 128 * 1024; sz >= 4096; sz -= 4096) {
                void *p = fw_malloc(sz);
                if (p) { big = sz; fw_free(p); break; }
            }
            void *chunks[24];
            int n = 0, total = 0;
            while (n < 24) {
                void *p = fw_malloc(8192);
                if (!p) break;
                chunks[n++] = p; total += 8192;
            }
            for (int i = 0; i < n; i++) fw_free(chunks[i]);
            plat_log("<HEAP наибольший кусок %d Б, кусками по 8К набрал %d Б>", big, total);
            break;
        }
        case 'G': {                     /* куда разведены все ножки */
            for (int i = 0; i < 22; i += 6) {
                plat_log("<OUTSEL %d:%d %d:%d %d:%d %d:%d %d:%d %d:%d>",
                         i, (int)(REG(0x60004554u + 4u * i) & 0xFF),
                         i + 1, (int)(REG(0x60004554u + 4u * (i + 1)) & 0xFF),
                         i + 2, (int)(REG(0x60004554u + 4u * (i + 2)) & 0xFF),
                         i + 3, (int)(REG(0x60004554u + 4u * (i + 3)) & 0xFF),
                         i + 4, (int)(REG(0x60004554u + 4u * (i + 4)) & 0xFF),
                         i + 5, (int)(REG(0x60004554u + 4u * (i + 5)) & 0xFF));
            }
            break;
        }
        case 'g': {                     /* как именно прошивка держит 21-ю ножку */
            unsigned iomux = REG(0x60009004u + 4u * 21);
            plat_log("<GPIO in=%x out=%x en=%x func21=%x iomux21=%x (mcu_sel=%d)>",
                     (unsigned)REG(0x6000403Cu), (unsigned)REG(0x60004004u),
                     (unsigned)REG(0x60004020u), (unsigned)REG(0x60004554u + 4u * 21),
                     iomux, (int)((iomux >> 12) & 7));
            break;
        }
        case 'k': {
            const volatile uint8_t *rb = (const volatile uint8_t *)FW_BUTTONS;
            plat_log("<KEYS %d %d %d %d  раскладка %d %d %d %d>", rb[0], rb[1], rb[2], rb[3],
                     btn_map[0], btn_map[1], btn_map[2], btn_map[3]);
            break;
        }
        case 'i':
            plat_log("<INFO стек %d, полоса %d строк, свободно %d Б, LVGL buf=%x %d px, "
                     "кадр %d мс (тактов %d, из них экран %d), кадров %d>",
                     (int)stack_bytes, g_band_h, mem_left(), (unsigned)(uint32_t)lv_buf,
                     (int)lv_buf_px, (int)frame_ms, (int)frame_cyc, (int)push_cyc,
                     (int)frames_total);
            break;
        case 'u': virt_keys = B_UP;    virt_until = now_ms() + 160; break;
        case 'd': virt_keys = B_DOWN;  virt_until = now_ms() + 160; break;
        case 'l': virt_keys = B_LEFT;  virt_until = now_ms() + 160; break;
        case 'r': virt_keys = B_RIGHT; virt_until = now_ms() + 160; break;
        case 'U': virt_keys = B_UP;    virt_until = now_ms() + 900; break;
        case 'D': virt_keys = B_DOWN;  virt_until = now_ms() + 900; break;
        case 'L': virt_keys = B_LEFT;  virt_until = now_ms() + 900; break;
        case 'R': virt_keys = B_RIGHT; virt_until = now_ms() + 900; break;
        default:
            if (c >= '0' && c <= '9') { zg_cmd_app = c - '0' + 1; zg_cmd_open = 1; }
            else if (c == 'a') { zg_cmd_app = 11; zg_cmd_open = 1; }
            else if (c == 'c') { zg_cmd_app = 12; zg_cmd_open = 1; }
        }
    }
}

/* ---- экран -------------------------------------------------------------- */
#define SPI_CHUNK 4800                /* max_transfer_sz прошивки */

static void lcd_push(int y0, int y1, const px *p)
{
    fw_lcd_setwindows(0, y0, SCR_W - 1, y1 - 1);
    fw_gpio_set_level(LCD_DC_GPIO, 1);
    int left = (y1 - y0) * SCR_W * 2;
    const uint8_t *src = (const uint8_t *)p;
    while (left > 0) {
        int n = left > SPI_CHUNK ? SPI_CHUNK : left;
        spi_transaction_t t;
        memset(&t, 0, sizeof t);
        t.length = (size_t)n * 8;
        t.tx_buffer = src;
        fw_spi_tx(&t);
        src += n; left -= n;
    }
}

static int push_y0, push_y1;

void fb_rows(int y0, int y1) { push_y0 = y0; push_y1 = y1; }

void fb_begin(void)
{
    uint32_t t = now_ms();
    frame_ms = t - frame_prev;
    frame_prev = t;
    frames_total++;
    poll_cmd();
    frame_cyc = fw_cycles() - cyc_mark;
    cyc_mark = fw_cycles();
    draw_mark = cyc_mark;
    if (frames_total & 1) push_cyc = 0;      /* мерим по одному кадру */
    if (zg_verbose && (frames_total & 31) == 0)   /* пульс только по команде 'v' */
    {
        /* счётчики DOOM: он ставится модулем, поэтому в ядре их может не быть */
        extern uint32_t dm_c_all __attribute__((weak));
        extern uint32_t dm_c_plane __attribute__((weak)), dm_c_wall __attribute__((weak)),
                        dm_c_seg __attribute__((weak)), dm_n_seg __attribute__((weak)),
                        dm_c_box __attribute__((weak));
        if (&dm_c_all)
            plat_log("~ %d мс: рендер %d = сегменты %d (%d шт) + пол %d + стены %d + узлы %d; экран %d",
                     (int)frame_ms, (int)dm_c_all, (int)dm_c_seg, (int)dm_n_seg,
                     (int)dm_c_plane, (int)dm_c_wall, (int)dm_c_box, (int)push_cyc);
        else
            plat_log("~ %d мс на кадр; экран %d", (int)frame_ms, (int)push_cyc);
        zg_sleep_cyc = 0;
    }
    cur.p = g_band; cur.y0 = 0; cur.y1 = g_band_h; started = 0;
    push_y0 = 0; push_y1 = SCR_H;
    shot_now = zg_shot;
    zg_shot = 0;
    if (shot_now) plat_log("<SHOT %d %d>", SCR_W, SCR_H);
}

band *fb_next(void)
{
    if (started) {
        uint32_t c0 = fw_cycles();
        int a = cur.y0 > push_y0 ? cur.y0 : push_y0;
        int b2 = cur.y1 < push_y1 ? cur.y1 : push_y1;
        if (a < b2) lcd_push(a, b2, cur.p + (a - cur.y0) * SCR_W);
        push_cyc += fw_cycles() - c0;
        if (shot_now && !usj_write((const uint8_t *)cur.p, (cur.y1 - cur.y0) * SCR_W * 2)) {
            shot_now = 0;      /* хост отвалился — кадр бросаем */
            plat_log("</SHOT ОБОРВАН>");
        }
        cur.y0 = cur.y1;
        cur.y1 = cur.y0 + g_band_h > SCR_H ? SCR_H : cur.y0 + g_band_h;
        if (cur.y0 >= SCR_H) {
            zg_draw_cyc = fw_cycles() - draw_mark;
            if (shot_now) { shot_now = 0; plat_log("</SHOT>"); }
            return 0;
        }
    }
    started = 1;
    return &cur;
}

/* ---- кнопки ------------------------------------------------------------- */
#define REPEAT_FIRST 380
#define REPEAT_NEXT  110

void in_poll(void)
{
    const volatile uint8_t *raw = (const volatile uint8_t *)FW_BUTTONS;
    uint32_t now = now_ms(), m = 0;
    poll_cmd();
    for (int i = 0; i < BTN_N; i++)
        if (raw[btn_map[i]]) m |= 1u << i;
    if (virt_keys) {
        if ((int32_t)(now - virt_until) < 0) m |= virt_keys;
        else virt_keys = 0;
    }
    hit = m & ~held;
    rep_mask = hit;
    for (int i = 0; i < BTN_N; i++) {
        uint32_t bit = 1u << i;
        if (hit & bit) { hold_since[i] = now; rep_at[i] = now + REPEAT_FIRST; }
        else if (m & bit) {
            if ((int32_t)(now - rep_at[i]) >= 0) { rep_mask |= bit; rep_at[i] = now + REPEAT_NEXT; }
        }
    }
    prev_held = held;
    held = m;
}

uint32_t in_held(void) { return held; }
uint32_t in_hit(void)  { return hit; }
uint32_t in_rep(void)  { return rep_mask; }
uint32_t in_held_ms(int b)
{ return (held & (1u << b)) ? now_ms() - hold_since[b] : 0; }

void in_set_map(int i, int raw) { if ((unsigned)i < BTN_N) btn_map[i] = (uint8_t)raw; }
int  in_get_map(int i) { return (unsigned)i < BTN_N ? btn_map[i] : 0; }
const volatile uint8_t *in_raw(void) { return (const volatile uint8_t *)FW_BUTTONS; }

/* ---- выход обратно в таймер --------------------------------------------- */
void plat_exit_to_stock(void)
{
    /* Во флеш отсюда не пишем: пока прошивка жива, любая запись роняет
     * систему.  Сохранение делает следующая загрузка — RTC-память её
     * переживает (см. nv_boot_io). */
    for (int i = 0; i < 12; i++) ((volatile uint8_t *)FW_BUTTONS)[i] = 0;  /* cur, flags, prev */
    zg_active = 0;
    zg_mute_keys = 0;
    fw_restart();          /* только так экран гарантированно возвращается */
    for (;;) ;
}

/* ---- задача ------------------------------------------------------------- */
extern uint8_t __bss_start__[], __bss_end__[];
extern uint8_t __persist_start__[], __persist_end__[];

#define PERSIST_MAGIC 0x5A47414Du     /* 'ZGAM' */

/* Прошивка отдаёт состояние только одной кнопки за раз (при нажатии она
 * обнуляет остальные байты), поэтому аккорд невозможен: входим долгим
 * нажатием «выбора» — это верхняя кнопка. */
#define MENU_BTN     (zm_cfg()->menu_btn)
#define MENU_HOLD_MS (zm_cfg()->menu_hold_ms)

/* Оперативку берём из кучи прошивки и не отдаём: выход из мода — перезагрузка,
 * так что «утечка» живёт ровно до неё.  Пробуем от большого к малому, первый
 * удачный кусок и есть вся наша память. */
static int arena_grab(void)
{
    static const uint32_t want[] = { 96 * 1024, 64 * 1024, 48 * 1024, 40 * 1024,
                                     32 * 1024, 24 * 1024, 16 * 1024, 12 * 1024, 8 * 1024 };
    for (unsigned i = 0; i < sizeof want / sizeof *want; i++) {
        void *p = fw_malloc(want[i]);
        if (p) { mem_init(p, (int)want[i]); return (int)want[i]; }
    }
    return 0;
}

static int lv_pool_added;

/* Готовим экран к работе: ждём, пока LVGL отдаст свой кадр (после этого он
 * заморожен), подключаем его буфер и берём полосу побольше. */
static void lv_lookup(void)
{
    const uint8_t *drv = (const uint8_t *)FW_LV_DISP_DRV;
    uint32_t hor = *(const uint16_t *)drv, ver = *((const uint16_t *)drv + 1);
    const uint8_t *db = *(const uint8_t **)(drv + 12);
    if (hor != SCR_W || ver != SCR_H || !db) return;
    void *b1 = *(void **)db;
    uint32_t sz = *(const uint32_t *)(db + 12);
    if ((uint32_t)b1 < 0x3FC8E200u || (uint32_t)b1 > 0x3FCDF000u) return;
    if (sz < SCR_W * 8 || sz > SCR_W * SCR_H) return;
    lv_buf = b1;
    lv_buf_px = sz;
}

/* Ищем lv_disp_t: это единственная структура, у которой первое поле —
 * указатель на наш lv_disp_drv_t.  Второе поле — таймер перерисовки; ставим
 * ему paused, и LVGL перестаёт трогать свой буфер (до перезагрузки). */
static int lv_stop_refresh(void)
{
    /* ниже 0x3FC8E200 читать нельзя: аппаратная защита памяти роняет в панику */
    for (uint32_t *p = (uint32_t *)0x3FC8E200u; p < (uint32_t *)0x3FCDF000u; p++) {
        if (*p != FW_LV_DISP_DRV) continue;
        uint32_t tmr = p[1];
        if (tmr < 0x3FC8E200u || tmr > 0x3FCDF000u || (tmr & 3)) continue;
        uint32_t period = *(uint32_t *)tmr, cb = *(uint32_t *)(tmr + 8);
        if (period < 2 || period > 2000) continue;
        if (cb < 0x42000000u || cb > 0x42200000u) continue;
        *(volatile uint32_t *)(tmr + 20) |= 1u;      /* lv_timer_t.paused */
        plat_log("LVGL остановлен: disp=%x таймер=%x период %d мс",
                 (unsigned)(uint32_t)p, (unsigned)tmr, (int)period);
        return 1;
    }
    plat_log("lv_disp_t не нашёлся — берём только половину буфера");
    return 0;
}

static void screen_prepare(void)
{
    if (!lv_buf) lv_lookup();
    if (lv_buf && lv_buf_px && !lv_pool_added) {
        int bytes = (int)lv_buf_px * 2;
        /* если остановить перерисовку не вышло, LVGL пишет свои области с
         * начала буфера — тогда берём только вторую половину */
        if (lv_stop_refresh()) mem_add(lv_buf, bytes);
        else mem_add((uint8_t *)lv_buf + bytes / 2, bytes / 2);
        lv_pool_added = 1;
    }
    mem_reset();
    /* Полоса должна покрывать экран целиком: иначе сцена рисуется столько раз,
     * сколько полос, а обход BSP в DOOM стоит дороже самих пикселей. */
    g_band_h = mem_biggest() / (SCR_W * 2);
    if (g_band_h > BAND_H) g_band_h = BAND_H;
    if (g_band_h < 2) g_band_h = 2;
    g_band = mem_alloc(SCR_W * g_band_h * 2);
    mem_lock();
    plat_log("экран: полоса %d строк, ОЗУ %d Б%s", g_band_h, mem_left(),
             lv_pool_added ? " (буфер LVGL наш)" : " (только своя память)");
}

static void zg_task(void *arg)
{
    stack_bytes = (int)(uint32_t)arg;
    fw_vtask_delay(600);              /* дать прошивке доинициализироваться */
    int arena_bytes = arena_grab();
    if (!arena_bytes) { plat_log("памяти не дали совсем — мод спит"); for (;;) fw_vtask_delay(200); }
    /* Часть стека отдаём в общий пул: восьми килобайт кучи играм мало.
     * Остатка (12 КБ) хватает и рекурсии по BSP в DOOM, и прерываниям. */
    uint8_t stack_pool[4096];
    mem_add(stack_pool, sizeof stack_pool);
    time_init();
    plat_srand(now_ms() ^ 0x9E3779B9u);
    if (cfg[CFG_INIT] != 0xA5) {              /* RTC-память чистая: было выключение */
        cfg[CFG_SND] = zm_cfg()->snd_on;
        cfg[CFG_MUTE] = zm_cfg()->mute_stock;
        cfg[CFG_INIT] = 0xA5;
    }
    snd_pin(SND_GPIO);
    snd_enable(cfg[CFG_SND]);
    snd_mute_stock(cfg[CFG_MUTE]);
    const volatile uint8_t *raw = (const volatile uint8_t *)FW_BUTTONS;
    plat_log("");
    plat_log("=== zeal games ===");
    plat_log("куча %d Б, стек %d Б; буфер LVGL подключим при открытии меню",
             arena_bytes, stack_bytes);
    plat_log("тик %d мс, тактов в мс по PCCR %d", (int)tick_ms, (int)cyc_per_ms);
    plat_log("кнопки %d %d %d %d, раскладка %d %d %d %d",
             raw[0], raw[1], raw[2], raw[3], btn_map[0], btn_map[1], btn_map[2], btn_map[3]);
    plat_log("вход в меню: держать кнопку «вверх» 1,5 с");

    uint32_t chord_from = 0, tick_count = 0;
    for (;;) {
        in_poll();
        if (zg_cmd_open) {
            zg_cmd_open = 0;
            zg_active = 1;
            zg_mute_keys = 1;
            screen_prepare();
            menu_run();
            zg_active = 0;
            chord_from = 0;
            continue;
        }
        zg_mute_keys = in_held_ms(MENU_BTN) > 350;
        if (in_held_ms(MENU_BTN) > MENU_HOLD_MS) {
            while (in_held()) { in_poll(); plat_sleep_ms(20); }
            zg_active = 1;
            zg_mute_keys = 1;
            screen_prepare();
            menu_run();                /* вернётся только через plat_exit_to_stock() */
            zg_active = 0;
            zg_mute_keys = 0;
        }
        (void)chord_from;
        wc_tick();                 /* часы идут и когда меню закрыто */
        (void)tick_count;
        plat_sleep_ms(30);
    }
}

__attribute__((used)) void zg_boot(void)
{
    zm_map_code();               /* код модулей — до первого обращения к нему */
    for (uint8_t *p = __bss_start__; p < __bss_end__; p++) *p = 0;
    /* .bss модулей Studio кладёт за областью, переживающей сброс */
    for (uint8_t *p = __persist_end__; p < (uint8_t *)(uintptr_t)zm_tab.bss_end; p++) *p = 0;
    nv_set_pending(__persist_magic[0] == PERSIST_MAGIC);   /* RTC пережила сброс */
    if (__persist_magic[0] != PERSIST_MAGIC) {        /* первый запуск после залива */
        __persist_magic[0] = PERSIST_MAGIC;
        for (uint32_t *p = (uint32_t *)__persist_start__; p < (uint32_t *)__persist_end__; p++) *p = 0;
        for (int i = 0; i < BTN_N; i++) btn_map[i] = zm_cfg()->btn_map[i];
    }
    cyc_per_ms = 160000;
    /* Флеш трогаем только здесь: прошивка ещё не подняла ни SPI, ни LVGL, а
     * позже любая запись роняет систему — её SPI-драйвер лезет во флеш из
     * прерывания, пока кэш выключен. */
    wc_boot();                   /* сначала привязать часы к новому счётчику */
    nv_boot_io();
    if (zg_cfg_stamp != zm_cfg()->stamp) {       /* Studio собрала образ заново */
        zg_cfg_stamp = zm_cfg()->stamp;
        for (int i = 0; i < BTN_N; i++) btn_map[i] = zm_cfg()->btn_map[i];
        cfg[CFG_SND] = zm_cfg()->snd_on;
        cfg[CFG_MUTE] = zm_cfg()->mute_stock;
        cfg[CFG_INIT] = 0xA5;
        nv_dirty();
    }

    /* стек задачи — единственная память, которую нам даёт прошивка: берём
     * столько, сколько отдаст куча, и живём внутри него */
    static const uint32_t want[] = { 16384, 14336, 12288, 10240, 8192 };
    for (unsigned i = 0; i < sizeof want / sizeof *want; i++)
        if (fw_task_create(zg_task, "zgames", want[i], (void *)want[i], 4, 0, 0) == 1) break;
}

/* ---- трамплины ----------------------------------------------------------
 * По четыре байта в прологах TDisplay_task и flush-колбэка LVGL заменены на
 * `j`; здесь мы доигрываем затёртые инструкции и возвращаемся.
 */
/* Пока меню наше, flush_ready НЕ зовём: LVGL остаётся с поднятым флагом
 * «идёт вывод», перестаёт перерисовываться — и его буфер кадра достаётся нам.
 * Заодно узнаём его размер: в lv_disp_drv_t поле draw_buf лежит по +12,
 * а в lv_disp_draw_buf_t — buf1/buf2/buf_act/size. */
/* Вызывается из трамплина на каждый flush.  Пока zg_active == 0 — только
 * запоминаем, где у LVGL буфер, и возвращаемся в его код. */
__attribute__((used)) void zg_lv_note(void *drv, void *color_p)
{
    if (!lv_seen) {
        lv_drv = drv;
        void *db = *(void **)((uint8_t *)drv + 12);
        uint32_t hor = *(uint16_t *)drv, ver = *((uint16_t *)drv + 1);
        void *b1 = db ? *(void **)db : 0;
        void *b2 = db ? *(void **)((uint8_t *)db + 4) : 0;
        uint32_t sz = db ? *(uint32_t *)((uint8_t *)db + 12) : 0;
        plat_log("LVGL: %dx%d draw_buf=%x buf1=%x buf2=%x size=%d px color_p=%x",
                 (int)hor, (int)ver, (unsigned)(uint32_t)db, (unsigned)(uint32_t)b1,
                 (unsigned)(uint32_t)b2, (int)sz, (unsigned)(uint32_t)color_p);
        if (hor == SCR_W && ver == SCR_H && sz >= SCR_W * 4 && sz <= SCR_W * SCR_H) {
            lv_buf = b1 ? b1 : color_p;
            lv_buf_px = sz;
        }
        lv_seen = 1;
    }
}

__attribute__((used)) void zg_flush_swallow(void *drv, void *area, void *color_p)
{
    (void)area;
    zg_lv_note(drv, color_p);
    zg_lv_frozen = 1;
    fw_lv_flush_ready(drv);     /* иначе главная задача прошивки крутится вхолостую */
}

/* Первое, что делает мод: отдаёт свободной записи MMU ту же страницу флеша,
 * где лежит .ptext2 — и весь остальной наш код становится исполняемым.  До
 * этой строчки вызывать из мода нельзя ничего: там ещё «недействительно».
 * t0/t1 в прологе чужой функции свободны. */
#define ZG_MAP_CODE \
        "li t0,%[slot]\n" \
        "li t1,%[page]\n" \
        "sw t1,0(t0)\n"

__attribute__((used, naked, section(".text.hooks"))) void zg_hook_boot(void)
{
    __asm__ volatile(
        ZG_MAP_CODE
        "addi sp,sp,-48\n"
        "sw ra,44(sp)\n sw a0,0(sp)\n sw a1,4(sp)\n sw a2,8(sp)\n sw a3,12(sp)\n"
        "sw a4,16(sp)\n sw a5,20(sp)\n sw a6,24(sp)\n sw a7,28(sp)\n"
        "call zg_boot\n"
        "lw ra,44(sp)\n lw a0,0(sp)\n lw a1,4(sp)\n lw a2,8(sp)\n lw a3,12(sp)\n"
        "lw a4,16(sp)\n lw a5,20(sp)\n lw a6,24(sp)\n lw a7,28(sp)\n"
        "addi sp,sp,48\n"
        "addi sp,sp,-64\n"          /* затёртое: пролог TDisplay_task */
        "sw s1,52(sp)\n"
        "li t0,%[resume]\n"
        "jr t0\n" :: [resume] "i"(HOOK_TDISPLAY_RESUME),
                      [slot] "i"(ZG_MMU_SLOT), [page] "i"(ZG_MMU_PAGE) : "memory");
}

/* пока mute — сразу в эпилог функции: регистры со стека она восстановит сама */
__attribute__((used, naked, section(".text.hooks"))) void zg_hook_keys(void)
{
    __asm__ volatile(
        "lui t0,%%hi(zg_mute_keys)\n"
        "lw  t0,%%lo(zg_mute_keys)(t0)\n"
        "bnez t0,1f\n"
        "lbu a4,0(s2)\n"           /* затёртое: чтение состояния кнопки */
        "li t0,%0\n"
        "jr t0\n"
        "1:\n"
        "li t0,%1\n"
        "jr t0\n" :: "i"(HOOK_KEYS_RESUME), "i"(HOOK_KEYS_EPILOGUE) : "memory");
}

__attribute__((used, naked, section(".text.hooks"))) void zg_hook_flush(void)
{
    __asm__ volatile(
        ZG_MAP_CODE
        "lui t0,%%hi(zg_active)\n"
        "lw  t0,%%lo(zg_active)(t0)\n"
        "bnez t0,1f\n"
        /* меню не наше: заглянуть в drv (один раз) и вернуться в LVGL */
        "lui t0,%%hi(zg_lv_seen)\n"
        "lw  t0,%%lo(zg_lv_seen)(t0)\n"
        "bnez t0,2f\n"
        "addi sp,sp,-16\n"
        "sw ra,12(sp)\n sw a0,0(sp)\n sw a1,4(sp)\n sw a2,8(sp)\n"
        "mv a1,a2\n"
        "call zg_lv_note\n"
        "lw ra,12(sp)\n lw a0,0(sp)\n lw a1,4(sp)\n lw a2,8(sp)\n"
        "addi sp,sp,16\n"
        "2:\n"
        "mv a5,a1\n"                /* затёртое: пролог flush-колбэка */
        "mv a4,a2\n"
        "li t0,%[resume]\n"
        "jr t0\n"
        "1:\n"
        "tail zg_flush_swallow\n" :: [resume] "i"(HOOK_LV_FLUSH_RESUME),
                                      [slot] "i"(ZG_MMU_SLOT), [page] "i"(ZG_MMU_PAGE) : "memory");
}
