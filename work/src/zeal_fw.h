/* zeal_fw.h - точки входа в стоковую прошивку Zeal Smart Timer (ESP32-C3).
 *
 * Адреса найдены разбором ota_0 (см. tools/img.py).  Ничего из прошивки не
 * пересобирается: мы только зовём её функции и подменяем два пролога.
 */
#ifndef ZEAL_FW_H
#define ZEAL_FW_H

#include <stdint.h>
#include <stddef.h>

/* --- адреса в стоковом образе ------------------------------------------- */
#define FW_LCD_SETWINDOWS      0x4201AC58u /* (x0,y0,x1,y1), сам шлёт RAMWR   */
#define FW_GPIO_SET_LEVEL      0x42048FC4u /* (gpio, level)                   */
#define FW_SPI_POLLING_TX      0x40382AEAu /* (handle, spi_transaction_t*)    */
#define FW_SPI_HANDLE_PTR      0x3FCAFEF4u /* void **                          */
#define FW_BUTTONS             0x3FCAFE20u /* uint8_t[4], живое состояние     */
#define FW_XTASKCREATEPINNED   0x40388676u
#define FW_VTASKDELAY          0x40388F40u
#define FW_LV_DISP_FLUSH_READY 0x42012556u
/* сам lv_disp_drv_t: прошивка кладёт flush_cb в drv+20 по адресу 0x42005E1E,
 * а в drv+12 — указатель на lv_disp_draw_buf_t (buf1, buf2, buf_act, size) */
#define FW_LV_DISP_DRV         0x3FC9043Cu
#define FW_ESP_RESTART_NOOS    0x403805B6u
#define FW_XTICKCOUNT          0x3FCAFDF0u /* uint32, счётчик тиков FreeRTOS */
#define FW_MALLOC              0x4038CC2Au /* pvPortMalloc(size) -> внутреннее ОЗУ */
/* Флеш: нашлись по хвостовым вызовам из esp_partition_write/erase_range.
 * Первый аргумент — «чип»; NULL означает основной. */
#define FW_FLASH_WRITE         0x4038425Eu /* (chip, buf, addr, len) */
#define FW_FLASH_ERASE         0x40383F1Eu /* (chip, addr, len) */
#define FW_FLASH_READ          0x4038412Au /* (chip, buf, addr, len) — функция перед write */
/* Последний сектор ota_1: в свой собственный раздел писать нельзя — ESP-IDF
 * считает это опасной записью и вызывает abort().  BLE_Update занимает начало
 * ota_1 и до конца раздела не достаёт. */
#define NVRAM_ADDR             0x3FF000u
#define FW_FREE                0x4038CC2Eu /* предположительно vPortFree(ptr) */

/* хуки: по 4 байта, заменяются на `j` */
#define HOOK_TDISPLAY_TASK     0x42004C76u
#define HOOK_TDISPLAY_RESUME   0x42004C7Au
#define HOOK_LV_FLUSH          0x42005D96u
#define HOOK_LV_FLUSH_RESUME   0x42005D9Au
/* цикл рассылки событий кнопок: состояние в 0x3FCAFE20 пишется до него, так
 * что глушим только рассылку — прошивка не узнает про наши долгие нажатия */
#define HOOK_KEYS              0x42005426u
#define HOOK_KEYS_RESUME       0x4200542Au
#define HOOK_KEYS_EPILOGUE     0x42005468u

#define LCD_DC_GPIO            9
#define FW_TICK_MS             5

/* --- прототипы ---------------------------------------------------------- */
typedef struct {
    uint32_t    flags;
    uint16_t    cmd;
    uint64_t    addr;
    size_t      length;      /* в БИТАХ */
    size_t      rxlength;
    void       *user;
    const void *tx_buffer;
    void       *rx_buffer;
} spi_transaction_t;

#define fw_call(addr, type) ((type)(addr))

static inline void fw_lcd_setwindows(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{ fw_call(FW_LCD_SETWINDOWS, void (*)(uint16_t, uint16_t, uint16_t, uint16_t))(x0, y0, x1, y1); }

static inline void fw_gpio_set_level(int gpio, uint32_t level)
{ fw_call(FW_GPIO_SET_LEVEL, void (*)(int, uint32_t))(gpio, level); }

static inline int fw_spi_tx(spi_transaction_t *t)
{ return fw_call(FW_SPI_POLLING_TX, int (*)(void *, spi_transaction_t *))(*(void **)FW_SPI_HANDLE_PTR, t); }

static inline void *fw_malloc(uint32_t n)
{ return fw_call(FW_MALLOC, void *(*)(uint32_t))(n); }

static inline void fw_free(void *p)
{ fw_call(FW_FREE, void (*)(void *))(p); }

/* «Чип» NULL функции не принимают; настоящий указатель лежит в описателе
 * раздела ota_0, который прошивка держит в памяти. */
void *fw_flash_chip(void);

static inline int fw_flash_write(uint32_t addr, const void *buf, uint32_t len)
{ return fw_call(FW_FLASH_WRITE, int (*)(void *, const void *, uint32_t, uint32_t))(fw_flash_chip(), buf, addr, len); }

static inline int fw_flash_erase(uint32_t addr, uint32_t len)
{ return fw_call(FW_FLASH_ERASE, int (*)(void *, uint32_t, uint32_t))(fw_flash_chip(), addr, len); }

static inline int fw_flash_read(uint32_t addr, void *buf, uint32_t len)
{ return fw_call(FW_FLASH_READ, int (*)(void *, void *, uint32_t, uint32_t))(fw_flash_chip(), buf, addr, len); }

static inline void fw_vtask_delay(uint32_t ticks)
{ fw_call(FW_VTASKDELAY, void (*)(uint32_t))(ticks); }

static inline void fw_lv_flush_ready(void *drv)
{ fw_call(FW_LV_DISP_FLUSH_READY, void (*)(void *))(drv); }

static inline void fw_restart(void)
{ fw_call(FW_ESP_RESTART_NOOS, void (*)(void))(); }

static inline int fw_task_create(void (*fn)(void *), const char *name, uint32_t stack,
                                 void *arg, uint32_t prio, void **handle, int core)
{ return fw_call(FW_XTASKCREATEPINNED,
        int (*)(void (*)(void *), const char *, uint32_t, void *, uint32_t, void **, int))
        (fn, name, stack, arg, prio, handle, core); }

/* Счётчик тактов ядра.  Стандартного mcycle у ESP32-C3 нет (проверено:
 * `csrr mcycle` роняет в Illegal instruction), зато есть свой PCCR — 0x7E2,
 * им же считает такты и сама прошивка. */
static inline uint32_t fw_cycles(void)
{ uint32_t v; __asm__ volatile("csrr %0, 0x7e2" : "=r"(v)); return v; }

#endif /* ZEAL_FW_H */
