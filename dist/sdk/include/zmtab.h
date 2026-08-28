/* zmtab.h — таблица ZealMod: единственное место, где прошивка и Studio
 * договариваются друг с другом.
 *
 * Ядро (этот код) собирается один раз и знает только, где лежит таблица.
 * Всё остальное — какие модули поставлены, какая тема, как назначены кнопки —
 * Studio дописывает прямо в готовый образ, не пересобирая ни строчки.
 * Поэтому раскладка структур обязана совпадать байт в байт с zealmod/tab.py.
 */
#ifndef ZMTAB_H
#define ZMTAB_H
#include <stdint.h>

#define ZM_VERSION   "0.9"
#define ZM_MAGIC     0x444F4D5Au   /* 'ZMOD' */
#define ZM_TAB_VER   1
#define ZM_ABI       1             /* версия ABI для модулей */
#define ZM_MAX_MODS  32
#define ZM_MAX_MMU   16
#define ZM_NAME_MAX  32
#define ZM_TITLE_MAX 24

/* Одна запись MMU: страницу флеша `page` показать по виртуальному адресу
 * 0x42000000 + entry*64K.  Так модули получают исполняемую память сверх тех
 * 47 КБ, что остались в хвосте стоковой IROM (см. tools/img.py, layout). */
typedef struct {
    uint16_t entry;
    uint16_t page;
} zm_map_t;

/* Оформление меню. Цвета — уже в формате кадра (RGB565 со сменой байт). */
typedef struct {
    uint32_t magic;
    uint8_t  layout;          /* 0 обложки, 1 сетка, 2 список */
    uint8_t  reflection;      /* высота отражения, % от обложки */
    uint8_t  flags;           /* 1 = обои, 2 = без подписи, 4 = без точек */
    uint8_t  spacing;         /* расстояние между обложками */
    uint16_t bg_top, bg_bot;  /* небо */
    uint16_t fl_top, fl_bot;  /* пол */
    uint16_t line;            /* горизонт */
    uint16_t accent;          /* выделение */
    uint16_t text, text_dim;
    uint16_t shadow;
    uint16_t pad0;
    uint32_t wallpaper;       /* const px* 240x240, иначе 0 */
    uint32_t logo;            /* const uint8_t* 8bpp, иначе своя заставка */
    uint32_t logo_pal;        /* const px* 256 */
    uint16_t logo_w, logo_h;
    char     name[ZM_NAME_MAX];
} zm_theme_t;

/* Один модуль — игра, приложение или что угодно с функцией run(). */
typedef struct {
    uint32_t run;             /* void (*)(void) */
    uint32_t title;           /* const char*, UTF-8 */
    uint32_t cover;           /* const uint8_t* 96x96, 8bpp */
    uint32_t pal;             /* const px* 256 */
    uint32_t id;              /* хэш строкового идентификатора */
    uint16_t flags;
    uint8_t  exit_btn;        /* какой кнопкой выходить */
    uint8_t  exit_hold;       /* десятые доли секунды */
} zm_mod_t;

/* Настройки по умолчанию: их Studio кладёт в образ, а часы — в свою NVRAM
 * при первом запуске после заливки. */
typedef struct {
    uint8_t  btn_map[4];      /* логическая кнопка -> байт состояния прошивки */
    uint8_t  menu_btn;        /* чем открывать меню */
    uint8_t  snd_on;
    uint8_t  mute_stock;      /* глушить сигнал прошивки на звуковой ножке */
    uint8_t  splash;          /* показывать заставку */
    uint16_t menu_hold_ms;    /* сколько держать кнопку, чтобы войти */
    uint16_t exit_hold_ms;    /* сколько держать, чтобы выйти из модуля */
    uint32_t stamp;           /* метка: поменялась — значит настройки новые */
    uint8_t  lang;            /* 0 английский, 1 русский */
    uint8_t  reserved[3];
} zm_cfg_t;

typedef struct {
    uint32_t   magic;
    uint32_t   n_map;
    zm_map_t   map[ZM_MAX_MMU];   /* смещение 8: его читает пролог zg_boot */
    uint32_t   version;
    uint32_t   abi;
    uint32_t   bss_end;           /* докуда чистить RTC (ядро + модули) */
    uint32_t   n_mods;
    zm_cfg_t   cfg;
    zm_theme_t theme;
    zm_mod_t   mods[ZM_MAX_MODS];
    char       build[24];         /* версия сборки, показывается в «О системе» */
} zm_tab_t;

extern const zm_tab_t zm_tab;

/* Ядро всегда обращается к таблице через эти обёртки: если Studio образ не
 * собирал (сборка разработчика), возвращаются встроенные значения. */
int  zm_count(void);
void zm_item(int i, zm_item_t *it);   /* пункт меню — уже в родных указателях */
const zm_theme_t *zm_theme(void);
const zm_cfg_t *zm_cfg(void);
int  zm_ready(void);          /* 1 = образ собран Studio */
void zm_map_code(void);       /* отобразить страницы кода модулей */
const char *zm_build(void);

#endif /* ZMTAB_H */
