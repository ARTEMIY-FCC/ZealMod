"""Эмулятор таймера: та же прошивка, собранная под компьютер.

Экран, кнопки и звук подменены на SDL, всё остальное — тот же код, что и на
часах: то же меню, те же шрифты, та же арифметика.  Поэтому программу удобно
писать и отлаживать здесь, а на часы заливать уже готовую.

  zealmod emu моя_игра/      посмотреть свою программу
  zealmod emu                всё, что есть встроенного
"""
import json
import subprocess
import sys
from pathlib import Path

from . import tab
from .mkmod import MkError, cover_from_png

CC = 'cc'
CORE = ['src/gfx.c', 'src/text.c', 'src/util.c', 'src/menu.c', 'src/gameutil.c',
        'src/zmtab.c', 'src/zmtab_data.c', 'src/splash.c', 'src/apps/wallclock.c',
        'build/fonts.c', 'build/tables.c',
        'host/host_sdl.c', 'host/snd_host.c', 'host/nvram_host.c']

KEYS = """\
controls: arrow keys are the timer buttons, Enter is ▲, Esc quits
"""


def _sdl_flags():
    try:
        cf = subprocess.run(['sdl2-config', '--cflags'], capture_output=True, text=True)
        lf = subprocess.run(['sdl2-config', '--libs'], capture_output=True, text=True)
        if cf.returncode or lf.returncode:
            raise FileNotFoundError
        return cf.stdout.split(), lf.stdout.split()
    except FileNotFoundError:
        raise MkError('SDL2 is missing (macOS: brew install sdl2, Debian/Ubuntu: apt install libsdl2-dev)') from None


def _covers_c(specs, cover_dir, out: Path):
    """Обложки модулей — в один .c, чтобы эмулятор показывал их как часы."""
    lines = ['/* сгенерировано zealmod emu */', '#include "plat.h"', '']
    for sp in specs:
        idx = pal = None
        cov = sp.get('cover')
        if cov:
            tries = [Path(cov), Path(sp['_dir']) / cov, Path(cover_dir) / cov,
                     Path(cover_dir) / f'{cov}.png']
            p = next((t for t in tries if t.is_file()), None)
            if p:
                idx, pal, _png = cover_from_png(p)
        name = sp['_sym']
        if idx is None:
            lines.append(f'const uint8_t cover_{name}[{96*96}];')
            lines.append(f'const px cover_{name}_pal[256];')
            continue
        lines.append(f'const uint8_t cover_{name}[{96*96}] = {{')
        lines += ['    ' + ','.join(str(b) for b in idx[i:i + 32]) + ','
                  for i in range(0, len(idx), 32)]
        lines.append('};')
        vals = [int.from_bytes(pal[i:i + 2], 'little') for i in range(0, len(pal), 2)]
        lines.append(f'const px cover_{name}_pal[256] = {{')
        lines += ['    ' + ','.join(f'0x{v:04x}' for v in vals[i:i + 8]) + ','
                  for i in range(0, len(vals), 8)]
        lines.append('};')
    out.write_text('\n'.join(lines) + '\n', 'utf-8')


def _apps_c(specs, out: Path):
    lines = ['/* сгенерировано zealmod emu */', '#include "plat.h"', '']
    for sp in specs:
        lines.append(f'void {sp["entry"]}(void);')
        lines.append(f'extern const uint8_t cover_{sp["_sym"]}[];')
        lines.append(f'extern const px cover_{sp["_sym"]}_pal[];')
    lines.append('')
    for sp in specs:
        lines.append(f'static const app_t app_{sp["_sym"]} = {{ "{sp["name"]}", '
                     f'cover_{sp["_sym"]}, cover_{sp["_sym"]}_pal, {sp["entry"]} }};')
    lines.append('')
    lines.append('const app_t *const apps[] = {')
    lines += [f'    &app_{sp["_sym"]},' for sp in specs]
    lines.append('};')
    lines.append('const int apps_n = (int)(sizeof apps / sizeof *apps);')
    out.write_text('\n'.join(lines) + '\n', 'utf-8')


def _specs(bundle, target):
    """Что показывать в эмуляторе: свой каталог или весь встроенный список."""
    out = []
    if target:
        d = Path(target)
        manf = d / 'module.json'
        if not manf.exists():
            raise MkError(f'{manf} not found — the emulator needs sources, not a .zm')
        sp = json.loads(manf.read_text('utf-8'))
        sp['_dir'] = d
        out.append(sp)
    else:
        spec = bundle.root / 'modules' / 'builtin.json'
        for sp in json.loads(spec.read_text('utf-8'))['modules']:
            sp['_dir'] = bundle.fw
            out.append(sp)
    for sp in out:
        sp['_sym'] = ''.join(c if c.isalnum() else '_' for c in sp['id'])
        sp.setdefault('entry', 'zm_main')
        sp.setdefault('name', sp['id'])
    return out


def emulate(bundle, target=None, extra=()):
    fw = bundle.fw
    if not (fw / 'host' / 'host_sdl.c').exists():
        raise MkError('the emulator needs the firmware sources (the work/ directory)')
    cflags, libs = _sdl_flags()
    build = fw / 'build' / 'emu'
    build.mkdir(parents=True, exist_ok=True)
    specs = _specs(bundle, target)
    _covers_c(specs, fw / 'assets' / 'covers', build / 'emu_covers.c')
    _apps_c(specs, build / 'emu_apps.c')

    srcs = [str(fw / s) for s in CORE]
    srcs += [str(build / 'emu_apps.c'), str(build / 'emu_covers.c')]
    for sp in specs:
        for s in sp['sources']:
            p = Path(sp['_dir']) / s
            if not p.exists():
                raise MkError(f'missing source {p}')
            if p.suffix == '.S':
                continue                 # ассемблер под RISC-V на компьютере не нужен
            srcs.append(str(p))
    inc = ['-I' + str(fw / 'src'), '-I' + str(fw / 'build'), '-I' + str(bundle.sdk)]
    for sp in specs:
        inc += ['-I' + str(Path(sp['_dir']) / i) for i in sp.get('include', [])]
        inc += ['-I' + str(Path(sp['_dir']) / 'src')]
    out = build / 'zealmod-emu'
    cmd = [CC, '-DPLAT_HOST=1', '-O2', '-g', '-w', *inc, *cflags,
           '-o', str(out), *srcs, *libs]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        raise MkError('build failed:\n' + (r.stderr or r.stdout)[-2500:])
    print(f'emulator: {out}')
    print(KEYS, end='')
    return subprocess.call([str(out), *extra])
