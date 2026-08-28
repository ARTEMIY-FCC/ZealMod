"""Таблица ZealMod: та же раскладка, что в work/src/zmtab.h.

Ядро прошивки читает эту структуру из флеша, Studio её туда пишет.  Любое
расхождение — и часы получат мусор вместо списка приложений, поэтому размеры
проверяются с обеих сторон (в C стоят _Static_assert).
"""
import struct

MAGIC = 0x444F4D5A          # 'ZMOD'
TAB_VER = 1
ABI = 1
MAX_MODS = 32
MAX_MMU = 16
NAME_MAX = 32
BUILD_MAX = 24

MAP_FMT = '<HH'
MAP_SIZE = 4
THEME_FMT = '<IBBBBHHHHHHHHHHIIIHH32s'
THEME_SIZE = 76
MOD_FMT = '<IIIIIHBB'
MOD_SIZE = 24
CFG_FMT = '<4sBBBBHHI4s'
CFG_SIZE = 20
TAB_SIZE = 8 + MAX_MMU * MAP_SIZE + 16 + CFG_SIZE + THEME_SIZE + MAX_MODS * MOD_SIZE + BUILD_MAX

assert struct.calcsize(THEME_FMT) == THEME_SIZE, struct.calcsize(THEME_FMT)
assert struct.calcsize(MOD_FMT) == MOD_SIZE
assert struct.calcsize(CFG_FMT) == CFG_SIZE
assert TAB_SIZE == 976


def rgb565(r, g, b):
    """Цвет в том виде, в каком его глотает экран: RGB565 со сменой байт."""
    v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
    return ((v >> 8) & 0xFF) | ((v & 0xFF) << 8)


def parse_color(c, default=(0, 0, 0)):
    """'#rrggbb', [r,g,b] или число."""
    if c is None:
        r, g, b = default
        return rgb565(r, g, b)
    if isinstance(c, int):
        return c & 0xFFFF
    if isinstance(c, (list, tuple)):
        return rgb565(*[int(v) & 0xFF for v in c[:3]])
    s = str(c).strip().lstrip('#')
    if len(s) == 3:
        s = ''.join(ch * 2 for ch in s)
    if len(s) != 6:
        raise ValueError(f'непонятный цвет: {c!r}')
    return rgb565(int(s[0:2], 16), int(s[2:4], 16), int(s[4:6], 16))


def unrgb565(v):
    """Обратно в (r, g, b) — для показа темы в интерфейсе."""
    v = ((v >> 8) & 0xFF) | ((v & 0xFF) << 8)
    return ((v >> 8) & 0xF8, (v >> 3) & 0xFC, (v << 3) & 0xF8)


def hex_color(v):
    r, g, b = unrgb565(v)
    return f'#{r:02x}{g:02x}{b:02x}'


def utf8(s, n):
    b = str(s).encode('utf-8')[:n - 1]
    return b + b'\0' * (n - len(b))


def pack_theme(t):
    return struct.pack(
        THEME_FMT, MAGIC,
        int(t.get('layout', 0)) & 0xFF, int(t.get('reflection', 42)) & 0xFF,
        int(t.get('flags', 0)) & 0xFF, int(t.get('spacing', 92)) & 0xFF,
        t['bg_top'], t['bg_bot'], t['fl_top'], t['fl_bot'], t['line'],
        t['accent'], t['text'], t['text_dim'], t['shadow'], 0,
        t.get('wallpaper', 0), t.get('logo', 0), t.get('logo_pal', 0),
        int(t.get('logo_w', 0)), int(t.get('logo_h', 0)),
        utf8(t.get('name', 'ZealMod'), NAME_MAX))


def pack_mod(m):
    return struct.pack(MOD_FMT, m['run'], m['title'], m['cover'], m['pal'],
                       m.get('id', 0) & 0xFFFFFFFF, int(m.get('flags', 0)),
                       int(m.get('exit_btn', 0)) & 0xFF,
                       int(m.get('exit_hold', 14)) & 0xFF)


def pack_cfg(c):
    btn = bytes(int(v) & 0xFF for v in c.get('btn_map', [0, 1, 2, 3]))[:4]
    btn += bytes(4 - len(btn))
    body = struct.pack('<4sBBBBHH', btn, int(c.get('menu_btn', 0)),
                       1 if c.get('snd_on', True) else 0,
                       1 if c.get('mute_stock', True) else 0,
                       1 if c.get('splash', True) else 0,
                       int(c.get('menu_hold_ms', 1500)),
                       int(c.get('exit_hold_ms', 1400)))
    # Метка: часы применяют настройки из образа, только если она изменилась.
    # Иначе после заливки старые значения из NVRAM затёрли бы новые.
    return body + struct.pack('<I4s', _stamp(body), b'\0' * 4)


def _stamp(body):
    h = 0x811C9DC5
    for b in body:
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h or 1


def pack_tab(*, mmu, cfg, theme, mods, bss_end, build):
    if len(mods) > MAX_MODS:
        raise ValueError(f'модулей больше {MAX_MODS}')
    if len(mmu) > MAX_MMU:
        raise ValueError(f'страниц кода больше {MAX_MMU}')
    out = struct.pack('<II', MAGIC, len(mmu))
    for entry, page in mmu:
        out += struct.pack(MAP_FMT, entry, page)
    out += b'\0' * ((MAX_MMU - len(mmu)) * MAP_SIZE)
    out += struct.pack('<IIII', TAB_VER, ABI, bss_end, len(mods))
    out += pack_cfg(cfg)
    out += pack_theme(theme)
    for m in mods:
        out += pack_mod(m)
    out += b'\0' * ((MAX_MODS - len(mods)) * MOD_SIZE)
    out += utf8(build, BUILD_MAX)
    assert len(out) == TAB_SIZE, len(out)
    return out


def mod_id(s: str) -> int:
    """FNV-1a: идентификатор модуля в виде числа (ключ для рекордов)."""
    h = 0x811C9DC5
    for b in str(s).encode('utf-8'):
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h


BUTTONS = {'up': 0, 'down': 1, 'left': 2, 'right': 3,
           'вверх': 0, 'вниз': 1, 'влево': 2, 'вправо': 3}


def button(v, default=0):
    if v is None:
        return default
    if isinstance(v, int):
        return v & 3
    return BUTTONS.get(str(v).strip().lower(), default)


DEFAULT_THEME = {
    'name': 'ZealMod',
    'layout': 0, 'reflection': 42, 'flags': 0, 'spacing': 92,
    'bg_top': rgb565(16, 18, 30), 'bg_bot': rgb565(3, 3, 6),
    'fl_top': rgb565(3, 3, 6), 'fl_bot': rgb565(14, 15, 24),
    'line': rgb565(44, 48, 66), 'accent': rgb565(230, 230, 240),
    'text': rgb565(255, 255, 255), 'text_dim': rgb565(60, 62, 74),
    'shadow': rgb565(0, 0, 0),
}
