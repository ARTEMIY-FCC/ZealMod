"""Форматы .zm (приложение) и .zt (тема).

Оба — обычные zip-архивы с описанием в JSON.  Приложение везёт внутри
перемещаемый объектный файл, обложку и, если нужно, свои данные; тема — цвета,
обои и логотип.  Ничего исполняемого на стороне компьютера в них нет.
"""
import json
import zipfile

from . import tab
from .build import Module
from .elf import Elf32
from .link import Linker

MODULE_FORMAT = 'zealmod-module/1'
THEME_FORMAT = 'zealmod-theme/1'
COVER_W = COVER_H = 96
COVER_SIZE = COVER_W * COVER_H
PAL_SIZE = 512


class PackError(Exception):
    pass


def _read(z, name, required=True):
    try:
        return z.read(name)
    except KeyError:
        if required:
            raise PackError(f'в архиве нет {name}') from None
        return b''


# --- приложения ------------------------------------------------------------
def read_module(path):
    """Разобрать .zm. Возвращает (Module, manifest)."""
    try:
        z = zipfile.ZipFile(path)
    except zipfile.BadZipFile:
        raise PackError('это не .zm — файл не открывается как архив') from None
    with z:
        try:
            man = json.loads(_read(z, 'manifest.json').decode('utf-8'))
        except json.JSONDecodeError as e:
            raise PackError(f'испорченный manifest.json: {e}') from None
        if man.get('format') != MODULE_FORMAT:
            raise PackError(f'неизвестный формат: {man.get("format")!r}')
        if not man.get('id'):
            raise PackError('в manifest.json нет id')
        obj = _read(z, 'code.o')
        cover = _read(z, 'cover.bin', required=False)
        pal = _read(z, 'cover.pal', required=False)
        if cover and len(cover) != COVER_SIZE:
            raise PackError(f'обложка должна быть {COVER_W}x{COVER_H} байт')
        if pal and len(pal) != PAL_SIZE:
            raise PackError('палитра обложки должна быть 512 байт')
        blobs = {}
        for name, src in (man.get('blobs') or {}).items():
            blobs[name] = _read(z, src)
        m = Module(id=man['id'], title=man.get('name') or man['id'], obj=obj,
                   cover=cover, pal=pal, blobs=blobs,
                   entry=man.get('entry', 'zm_main'),
                   exit_btn=tab.button(man.get('exit_button'), 0),
                   exit_hold=max(1, min(255, int(round(
                       float(man.get('exit_hold', 1.4)) * 10)))),
                   abi=int(man.get('abi', 1)), version=str(man.get('version', '')),
                   author=str(man.get('author', '')), source=str(path))
    return m, man


def module_preview(path):
    """PNG-обложка для интерфейса (или пусто)."""
    with zipfile.ZipFile(path) as z:
        return _read(z, 'cover.png', required=False)


def write_module(path, manifest, obj, *, cover=b'', pal=b'', cover_png=b'', blobs=None):
    manifest = dict(manifest)
    manifest['format'] = MODULE_FORMAT
    blobs = blobs or {}
    manifest['blobs'] = {name: f'blobs/{name}.bin' for name in sorted(blobs)}
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as z:
        z.writestr('manifest.json', json.dumps(manifest, ensure_ascii=False, indent=2))
        z.writestr('code.o', obj)
        if cover:
            z.writestr('cover.bin', cover)
            z.writestr('cover.pal', pal)
        if cover_png:
            z.writestr('cover.png', cover_png)
        for name, blob in sorted(blobs.items()):
            z.writestr(f'blobs/{name}.bin', blob)


def check_module(m: Module, exports: dict, abi=tab.ABI):
    """Совместим ли модуль с этим ядром: что просит и всё ли ядро умеет."""
    out = dict(ok=False, id=m.id, title=m.title, missing=[], imports=[],
               code=0, data=0, bss=0, problems=[])
    if m.abi > abi:
        out['problems'].append(f'модуль просит ABI {m.abi}, а ядро умеет {abi}')
    try:
        elf = Elf32(m.obj, m.id)
    except ValueError as e:
        out['problems'].append(str(e))
        return out
    if elf.etype != 1:
        out['problems'].append('code.o должен быть перемещаемым объектным файлом')
    known = set(exports) | set(m.blobs)
    have = {s.name for s in elf.symbols if s.defined and s.name}
    for s in elf.symbols:
        if s.defined or not s.name or s.name in have:
            continue
        (out['imports'] if s.name in known else out['missing']).append(s.name)
    out['imports'] = sorted(set(out['imports']))
    out['missing'] = sorted(set(out['missing']))
    if out['missing']:
        out['problems'].append('ядро не знает: ' + ', '.join(out['missing'][:6]) +
                               ('…' if len(out['missing']) > 6 else ''))
    for s in elf.sections:
        if not s.alloc or not s.size:
            continue
        r = Linker.region(s)
        if r is None:
            out['problems'].append(f'секция {s.name} изменяемая — так нельзя')
        elif r == 'text':
            out['code'] += s.size
        elif r == 'rodata':
            out['data'] += s.size
        else:
            out['bss'] += s.size
    if m.entry not in have:
        out['problems'].append(f'нет функции {m.entry}()')
    out['data'] += len(m.cover) + len(m.pal) + sum(len(b) for b in m.blobs.values())
    out['ok'] = not out['problems']
    return out


# --- темы ------------------------------------------------------------------
def read_theme(path):
    """Разобрать .zt: словарь для tab.pack_theme + сырые картинки."""
    try:
        z = zipfile.ZipFile(path)
    except zipfile.BadZipFile:
        raise PackError('это не .zt — файл не открывается как архив') from None
    with z:
        try:
            src = json.loads(_read(z, 'theme.json').decode('utf-8'))
        except json.JSONDecodeError as e:
            raise PackError(f'испорченный theme.json: {e}') from None
        if src.get('format') != THEME_FORMAT:
            raise PackError(f'неизвестный формат темы: {src.get("format")!r}')
        wall = _read(z, 'wallpaper.bin', required=False)
        logo = _read(z, 'logo.bin', required=False)
        logo_pal = _read(z, 'logo.pal', required=False)
    if wall and len(wall) != 240 * 240 * 2:
        raise PackError('обои должны быть 240x240 (115200 байт)')
    t = theme_from_json(src)
    return t, dict(wallpaper=wall, logo=logo, logo_pal=logo_pal,
                   logo_w=int(src.get('logo_w', 0)), logo_h=int(src.get('logo_h', 0))), src


LAYOUTS = {'coverflow': 0, 'обложки': 0, 'grid': 1, 'сетка': 1, 'list': 2, 'список': 2}


def theme_from_json(src):
    c = src.get('colors') or {}
    d = tab.DEFAULT_THEME
    t = dict(d)
    t['name'] = str(src.get('name', 'ZealMod'))[:31]
    lay = src.get('layout', 0)
    t['layout'] = LAYOUTS.get(str(lay).lower(), lay if isinstance(lay, int) else 0)
    t['reflection'] = int(src.get('reflection', 42))
    t['spacing'] = int(src.get('spacing', 92))
    flags = 0
    if src.get('wallpaper'):
        flags |= 1
    if src.get('hide_title'):
        flags |= 2
    if src.get('hide_dots'):
        flags |= 4
    t['flags'] = flags
    for k in ('bg_top', 'bg_bot', 'fl_top', 'fl_bot', 'line', 'accent', 'text',
              'text_dim', 'shadow'):
        if k in c:
            t[k] = tab.parse_color(c[k])
    return t


def write_theme(path, src, *, wallpaper=b'', logo=b'', logo_pal=b'', preview=b''):
    src = dict(src)
    src['format'] = THEME_FORMAT
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as z:
        z.writestr('theme.json', json.dumps(src, ensure_ascii=False, indent=2))
        if wallpaper:
            z.writestr('wallpaper.bin', wallpaper)
        if logo:
            z.writestr('logo.bin', logo)
            z.writestr('logo.pal', logo_pal)
        if preview:
            z.writestr('preview.png', preview)


def theme_preview(path):
    with zipfile.ZipFile(path) as z:
        return _read(z, 'preview.png', required=False)
