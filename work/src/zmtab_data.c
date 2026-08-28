/* zmtab_data.c — сама таблица ZealMod и больше ничего.
 *
 * Живёт отдельным файлом не из любви к порядку: таблица объявлена const, и
 * компилятор, увидев рядом с обращением к ней нулевой инициализатор, честно
 * свернёт все проверки в «модулей нет» — что и случилось на живых часах.
 * Пока определение лежит в своей единице трансляции, а читают его из других,
 * свернуть нечего: значения приходят из флеша, куда их пишет Studio.
 */
#include <stddef.h>
#include "plat.h"
#include "zmtab.h"

/* Раскладку читает и пишет ещё и Studio (studio/zealmod/tab.py) — если тут
 * что-то сдвинется, часы получат мусор вместо списка приложений. */
_Static_assert(sizeof(zm_tab_t) == 976, "zm_tab_t разъехалась с tab.py");
_Static_assert(sizeof(zm_theme_t) == 76, "zm_theme_t разъехалась с tab.py");
_Static_assert(sizeof(zm_mod_t) == 24, "zm_mod_t разъехалась с tab.py");
_Static_assert(sizeof(zm_cfg_t) == 20, "zm_cfg_t разъехалась с tab.py");
_Static_assert(offsetof(zm_tab_t, map) == 8, "map читает пролог загрузки");
_Static_assert(offsetof(zm_tab_t, n_mods) == 84, "n_mods разъехалось с tab.py");

/* На компьютере (стенд и эмулятор) своей секции нет и не нужно. */
#ifdef PLAT_HOST
#define ZMTAB_PLACE __attribute__((used, aligned(4)))
#else
#define ZMTAB_PLACE __attribute__((section(".zmtab"), used, aligned(4)))
#endif

ZMTAB_PLACE const zm_tab_t zm_tab = { .magic = 0 };
