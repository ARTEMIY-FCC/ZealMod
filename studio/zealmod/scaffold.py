"""Заготовки: `zealmod new app` и `zealmod new theme`.

Получается каталог, который сразу собирается: `zealmod pack мояигра` даёт .zm,
который можно поставить на любые часы через Studio.
"""
import json
import zipfile
from pathlib import Path

from . import tab

APP_C = '''/* {name} — программа для ZealMod.
 *
 * Всё, что можно звать из модуля, объявлено в plat.h, game.h, fx.h и snd.h.
 * Правила простые:
 *   - изменяемых данных с начальным значением быть не должно (их некому
 *     копировать в ОЗУ): либо const, либо обнулённые;
 *   - память берётся у mem_alloc(), она обнулена и живёт до выхода;
 *   - кадр рисуется в fb_begin()/fb_next(), между кадрами обязателен
 *     game_frame_wait(), иначе часы решат, что мод завис.
 */
#include "plat.h"
#include "game.h"
#include "fx.h"
#include "snd.h"

void zm_main(void)
{{
    game_exit_button(BTN_UP);        /* этой кнопкой выходят: держать 1,4 с */
    int x = SCR_W / 2, y = SCR_H / 2;
    uint32_t last = now_ms();

    while (!game_quit()) {{
        in_poll();
        uint32_t hit = in_rep();
        if (hit & B_LEFT)  x -= 8;
        if (hit & B_RIGHT) x += 8;
        if (hit & B_DOWN)  y += 8;
        x = iclamp(x, 20, SCR_W - 20);
        y = iclamp(y, 20, SCR_H - 20);
        if (hit) SND(snd_click);

        fb_begin();
        for (band *b; (b = fb_next()); ) {{
            gfx_clear(b, RGB(12, 14, 22));
            gfx_disc(b, x, y, 16, RGB(110, 168, 255));
            gfx_text_c(b, SCR_W / 2, 36, &font_m, WHITE, "{name}");
            gfx_text_c(b, SCR_W / 2, 214, &font_s, RGB(140, 146, 162),
                       "◀ ▶ двигать, ▲ держать — выход");
        }}
        game_frame_wait(&last, 33);   /* ~30 кадров в секунду */
    }}
}}
'''

MODULE_JSON = {
    'id': 'myapp', 'name': 'Моя программа', 'version': '1.0', 'author': 'я',
    'kind': 'game', 'entry': 'zm_main', 'exit_button': 'up', 'exit_hold': 1.4,
    'description': 'Что эта программа делает.',
    'cover': 'cover.png', 'sources': ['src/main.c'],
}

README = '''# {name}

Программа для ZealMod (таймер Zeal).

    zealmod pack .            # собрать {id}.zm
    zealmod check {id}.zm     # проверить совместимость
    zealmod emu .             # посмотреть на компьютере

Готовый .zm перетащите в окно ZealMod Studio — и он окажется в списке программ.

Что можно звать из кода — см. `sdk/include/plat.h` и `game.h`.
'''

THEME_JSON = {
    'format': 'zealmod-theme/1',
    'name': 'Моя тема', 'author': 'я', 'layout': 'coverflow',
    'reflection': 42, 'spacing': 92,
    'colors': {
        'bg_top': '#101220', 'bg_bot': '#030306',
        'fl_top': '#030306', 'fl_bot': '#0e0f18',
        'line': '#2c3042', 'accent': '#e6e6f0',
        'text': '#ffffff', 'text_dim': '#3c3e4a', 'shadow': '#000000',
    },
}


def new_app(path: Path):
    path = Path(path)
    (path / 'src').mkdir(parents=True, exist_ok=True)
    name = path.name
    man = dict(MODULE_JSON, id=_slug(name), name=name)
    (path / 'module.json').write_text(json.dumps(man, ensure_ascii=False, indent=2), 'utf-8')
    (path / 'src' / 'main.c').write_text(APP_C.format(name=name), 'utf-8')
    (path / 'README.md').write_text(README.format(name=name, id=man['id']), 'utf-8')
    _cover(path / 'cover.png', name)
    return path


def new_theme(path: Path):
    path = Path(path)
    path.mkdir(parents=True, exist_ok=True)
    src = dict(THEME_JSON, name=path.name)
    (path / 'theme.json').write_text(json.dumps(src, ensure_ascii=False, indent=2), 'utf-8')
    return path


def pack_theme_dir(path: Path, out: Path = None):
    """Каталог с theme.json (и, если есть, wallpaper.png/logo.png) -> .zt"""
    path = Path(path)
    src = json.loads((path / 'theme.json').read_text('utf-8'))
    src['format'] = 'zealmod-theme/1'
    wall = logo = logo_pal = preview = b''
    wp = path / 'wallpaper.png'
    if wp.exists():
        wall = _wallpaper(wp)
        src['wallpaper'] = True
    lp = path / 'logo.png'
    if lp.exists():
        from .mkmod import cover_from_png
        logo, logo_pal, preview = cover_from_png(lp, size=128)
        src['logo_w'] = src['logo_h'] = 128
    out = Path(out or path.with_suffix('.zt'))
    out.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
        z.writestr('theme.json', json.dumps(src, ensure_ascii=False, indent=2))
        if wall:
            z.writestr('wallpaper.bin', wall)
        if logo:
            z.writestr('logo.bin', logo)
            z.writestr('logo.pal', logo_pal)
        if preview:
            z.writestr('preview.png', preview)
    return out


def _wallpaper(png: Path):
    """PNG 240x240 -> сырые пиксели экрана (RGB565, старший байт вперёд)."""
    from PIL import Image
    im = Image.open(png).convert('RGB').resize((240, 240))
    out = bytearray()
    for r, g, b in im.getdata():
        out += tab.rgb565(r, g, b).to_bytes(2, 'little')
    return bytes(out)


def _slug(s):
    ok = 'abcdefghijklmnopqrstuvwxyz0123456789_'
    s = ''.join(c if c in ok else '_' for c in str(s).lower())
    return s.strip('_') or 'myapp'


def _cover(path: Path, name):
    """Простая обложка, чтобы модуль сразу был на что-то похож."""
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        return
    im = Image.new('RGB', (96, 96))
    d = ImageDraw.Draw(im)
    for y in range(96):
        t = y / 95
        d.line([(0, y), (96, y)], fill=(int(24 + 40 * t), int(28 + 30 * t), int(52 + 60 * t)))
    d.rounded_rectangle([22, 22, 74, 74], 12, outline=(150, 190, 255), width=4)
    d.text((30, 40), name[:5], fill=(230, 236, 255))
    im.save(path)
