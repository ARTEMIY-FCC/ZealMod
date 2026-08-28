/* zmtab.c — таблица ZealMod и всё, что делает образ настраиваемым.
 *
 * Сама таблица лежит в отдельной секции .zmtab: Studio находит её по символу
 * и переписывает в готовом образе.  Если образ собирала не Studio (обычный
 * `make` разработчика), таблица пустая — тогда ядро берёт встроенный список
 * приложений и тему по умолчанию.
 */
#include "plat.h"
#include "zmtab.h"

/* Сама таблица определена в zmtab_data.c: если бы её инициализатор был виден
 * отсюда, компилятор свернул бы все проверки в «таблица пуста». */

/* Тема по умолчанию — та самая тёмная сцена с отражениями. */
static const zm_theme_t theme_default = {
    .magic = ZM_MAGIC,
    .layout = 0,
    .reflection = 42,
    .flags = 0,
    .spacing = 92,
    .bg_top = RGB(16, 18, 30), .bg_bot = RGB(3, 3, 6),
    .fl_top = RGB(3, 3, 6),    .fl_bot = RGB(14, 15, 24),
    .line = RGB(44, 48, 66),
    .accent = RGB(230, 230, 240),
    .text = RGB(255, 255, 255),
    .text_dim = RGB(60, 62, 74),
    .shadow = RGB(0, 0, 0),
    .name = "ZealMod",
};

#ifdef PLAT_HOST
int zm_lang_force = -1;                   /* эмулятор умеет задать язык ключом */
#endif

static const zm_cfg_t cfg_default = {
    .btn_map = { 0, 1, 2, 3 },
    .menu_btn = BTN_UP,
    .snd_on = 1,
    .mute_stock = 1,
    .splash = 1,
    .menu_hold_ms = 1500,
    .exit_hold_ms = 1400,
    .lang = ZM_EN,
};

int zm_lang(void)
{
#ifdef PLAT_HOST
    if (zm_lang_force >= 0) return zm_lang_force;
#endif
    int l = zm_cfg()->lang;
    return (unsigned)l < ZM_LANGS ? l : ZM_EN;
}

const char *zm_tr(const char *en, const char *ru)
{
    return zm_lang() == ZM_RU ? ru : en;
}

int zm_ready(void) { return zm_tab.magic == ZM_MAGIC && zm_tab.n_mods > 0; }

int zm_count(void)
{
    if (zm_ready())
        return (int)(zm_tab.n_mods > ZM_MAX_MODS ? ZM_MAX_MODS : zm_tab.n_mods);
    return apps_n;
}

void zm_item(int i, zm_item_t *it)
{
    it->title = "";
    it->cover = 0;
    it->pal = 0;
    it->run = 0;
    it->exit_btn = BTN_UP;
    it->exit_hold = 14;
    if (zm_ready()) {
        if ((unsigned)i >= zm_tab.n_mods) return;
        const zm_mod_t *m = &zm_tab.mods[i];
        it->title = (const char *)(uintptr_t)m->title;
        it->cover = (const uint8_t *)(uintptr_t)m->cover;
        it->pal = (const px *)(uintptr_t)m->pal;
        it->run = (void (*)(void))(uintptr_t)m->run;
        it->exit_btn = m->exit_btn;
        it->exit_hold = m->exit_hold ? m->exit_hold : 14;
    } else if ((unsigned)i < (unsigned)apps_n) {
        const app_t *a = apps[i];
        it->title = a->title;
        it->cover = a->cover;
        it->pal = a->cover_pal;
        it->run = a->run;
    }
}

const zm_theme_t *zm_theme(void)
{
    return zm_tab.theme.magic == ZM_MAGIC ? &zm_tab.theme : &theme_default;
}

const zm_cfg_t *zm_cfg(void)
{
    return zm_tab.magic == ZM_MAGIC ? &zm_tab.cfg : &cfg_default;
}

const char *zm_build(void)
{
    return zm_tab.magic == ZM_MAGIC && zm_tab.build[0] ? zm_tab.build : ZM_VERSION;
}

#ifndef PLAT_HOST
#define MMU_TABLE 0x600C5000u      /* таблица MMU: см. tools/img.py */

/* Код модулей лежит внутри сегмента данных: чтобы его можно было исполнять,
 * страницы флеша показываются ещё раз через свободные записи MMU.  Страница
 * ядра уже отображена трамплином — здесь только то, что сверх неё. */
void zm_map_code(void)
{
    if (zm_tab.magic != ZM_MAGIC) return;
    uint32_t n = zm_tab.n_map > ZM_MAX_MMU ? ZM_MAX_MMU : zm_tab.n_map;
    for (uint32_t i = 0; i < n; i++)
        *(volatile uint32_t *)(MMU_TABLE + zm_tab.map[i].entry * 4u) = zm_tab.map[i].page;
}
#else
void zm_map_code(void) { }
#endif
