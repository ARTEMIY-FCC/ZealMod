"""Командная строка ZealMod.

  zealmod devices                 кто подключён по USB
  zealmod mods                    собрать встроенные приложения в .zm
  zealmod build --all -o out.gbl  собрать образ
  zealmod check игра.zm           совместима ли программа с этими часами
  zealmod pack каталог/           упаковать свою программу в .zm
  zealmod new app мояигра         заготовка новой программы
  zealmod flash out.gbl           залить в часы
  zealmod studio                  графическая программа в браузере
"""
import argparse
import json
import sys
from pathlib import Path

from . import __version__, pack, tab
from .build import BuildError, build
from .res import Bundle


def human(n):
    return f'{n/1024:.1f} КБ' if n >= 1024 else f'{n} Б'


def _bundle(args):
    return Bundle(getattr(args, 'bundle', None))


# --- команды ---------------------------------------------------------------
def cmd_devices(args):
    from . import device
    ps = device.ports()
    if not ps:
        print('часов не вижу. Подключи кабелем, который умеет данные, и повтори.')
        return 1
    for p in ps:
        mark = '*' if p['score'] else ' '
        print(f'{mark} {p["port"]:<28} {p["vid"]}:{p["pid"]} {p["desc"]}')
    if not device.esptool_ok():
        print('\n! для заливки нужен esptool: pip install esptool')
    return 0


def cmd_mods(args):
    from . import mkmod
    b = _bundle(args)
    specs = mkmod.load_specs(b.root / 'modules' / 'builtin.json')
    only = set(args.only.split(',')) if args.only else None
    out = b.dist / 'modules'
    work = b.fw / 'build' / 'mods'
    covers = b.fw / 'assets' / 'covers'
    done = 0
    for spec in specs:
        if only and spec['id'] not in only:
            continue
        try:
            p = mkmod.build_module(spec, b.fw, out, covers, work)
        except mkmod.MkError as e:
            print(f'{spec["id"]}: {e}')
            return 1
        print(f'{spec["id"]:<10} -> {p.relative_to(b.root)}  {human(p.stat().st_size)}')
        done += 1
    # темы из themes/*/theme.json — в тот же комплект
    from .scaffold import pack_theme_dir
    for td in sorted((b.root / 'themes').glob('*/theme.json')):
        try:
            zt = pack_theme_dir(td.parent, b.dist / 'themes' / f'{td.parent.name}.zt')
            print(f'{td.parent.name:<10} -> {zt.relative_to(b.root)}')
        except Exception as e:
            print(f'{td.parent.name}: тема не собралась — {e}')
    # заголовки, по которым собираются чужие модули
    sdk = b.dist / 'sdk' / 'include'
    sdk.mkdir(parents=True, exist_ok=True)
    for h in sorted((b.fw / 'src').glob('*.h')):
        (sdk / h.name).write_bytes(h.read_bytes())
    # ядро и стоковый образ кладём рядом, чтобы комплект был самодостаточным
    (b.dist / 'core').mkdir(parents=True, exist_ok=True)
    (b.dist / 'base').mkdir(parents=True, exist_ok=True)
    core = b.fw / 'build' / 'core.elf'
    if core.exists():
        (b.dist / 'core' / 'zealmod-core.elf').write_bytes(core.read_bytes())
    stock = b.root / 'original.gbl'
    if stock.exists() and not (b.dist / 'base' / 'zeal-stock.gbl').exists():
        (b.dist / 'base' / 'zeal-stock.gbl').write_bytes(stock.read_bytes())
    print(f'готово: {done} модулей')
    return 0


def _default_order(b):
    """Порядок как в списке встроенных, остальные — следом по алфавиту."""
    have = [p.stem for p in b.modules()]
    order = []
    spec = b.root / 'modules' / 'builtin.json'
    if spec.exists():
        try:
            order = [m['id'] for m in json.loads(spec.read_text('utf-8'))['modules']]
        except (KeyError, json.JSONDecodeError):
            order = []
    known = [i for i in order if i in have]
    return known + sorted(set(have) - set(known))


def _load_modules(b, names):
    """names — идентификаторы из каталога или пути к .zm."""
    have = {p.stem: p for p in b.modules()}
    out = []
    for n in names:
        p = Path(n)
        if not p.exists():
            p = have.get(n)
        if not p:
            raise BuildError(f'не нашёл модуль {n}')
        m, _ = pack.read_module(p)
        out.append(m)
    return out


def cmd_build(args):
    b = _bundle(args)
    prof = b.profile(args.profile)
    names = list(args.mods.split(',')) if args.mods else []
    if args.all or not names:
        names = _default_order(b)
    if not names:
        print('модулей нет: собери их командой `zealmod mods`')
        return 1
    mods = _load_modules(b, names)
    theme, assets, _src = (tab.DEFAULT_THEME, {}, {})
    if args.theme:
        theme, assets, _src = pack.read_theme(args.theme)
    cfg = {}
    for kv in args.set or []:
        k, _, v = kv.partition('=')
        v = v.strip()
        if ',' in v:                       # btn_map=3,2,1,0
            cfg[k.strip()] = [int(x) for x in v.split(',')]
        elif v.lstrip('-').isdigit():
            cfg[k.strip()] = int(v)
        else:
            cfg[k.strip()] = v
    ok, got, want = b.base_ok(prof)
    if not ok and not args.force:
        print(f'! стоковый образ не тот, под который собран мод\n  есть  {got}\n'
              f'  нужен {want}\n  (--force, если уверен)')
        return 1
    try:
        image, rep = build(b.base.read_bytes(), b.core.read_bytes(), mods, theme=theme,
                           cfg=cfg, profile=prof, theme_assets=assets)
    except BuildError as e:
        print(f'не собралось: {e}')
        return 1
    out = Path(args.out or (b.dist / 'zealmod.gbl'))
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(image)
    for m in rep.modules:
        print(f'  {m["id"]:<10} код {human(m["code"]):>10}  данные {human(m["data"]):>10}'
              f'  RTC {m["bss"]} Б')
    print(f'{out}: {human(rep.size)}, свободно {human(rep.free)}; '
          f'страниц кода {rep.pages}, RTC под модули {rep.bss} Б')
    return 0


def cmd_check(args):
    b = _bundle(args)
    from .elf import Elf32
    core = Elf32(b.core.read_bytes(), 'core.elf')
    exports = {s.name: s.value for s in core.symbols if s.defined and s.global_ and s.name}
    rc = 0
    for path in args.files:
        m, man = pack.read_module(path)
        r = pack.check_module(m, exports)
        print(f'{m.id} «{m.title}» {man.get("version", "")} — '
              f'{"подходит" if r["ok"] else "НЕ ПОДХОДИТ"}')
        print(f'  код {human(r["code"])}, данные {human(r["data"])}, RTC {r["bss"]} Б, '
              f'обращений к ядру {len(r["imports"])}')
        for p in r['problems']:
            print(f'  ! {p}')
        rc |= 0 if r['ok'] else 1
    return rc


def cmd_pack(args):
    from . import mkmod
    b = _bundle(args)
    src = Path(args.dir)
    manf = src / 'module.json'
    if not manf.exists():
        print(f'нет {manf}')
        return 1
    spec = json.loads(manf.read_text('utf-8'))
    out = Path(args.out or b.dist / 'modules')
    try:
        p = mkmod.build_module(spec, src, out, src, src / 'build',
                               includes=[b.sdk])
    except mkmod.MkError as e:
        print(e)
        return 1
    print(f'{p}  {human(p.stat().st_size)}')
    return 0


def cmd_new(args):
    from .scaffold import new_app, new_theme
    p = (new_app if args.kind == 'app' else new_theme)(Path(args.name))
    print(f'создано: {p}')
    print(f'дальше:  zealmod {"pack" if args.kind == "app" else "theme"} {p}')
    return 0


def cmd_theme(args):
    from .scaffold import pack_theme_dir
    p = pack_theme_dir(Path(args.dir), Path(args.out) if args.out else None)
    print(f'{p}')
    return 0


def cmd_flash(args):
    from . import device
    b = _bundle(args)
    img = Path(args.image or (b.dist / 'zealmod.gbl'))
    if not img.exists():
        print(f'нет образа {img}')
        return 1
    ps = device.ports()
    port = args.port or (ps[0]['port'] if ps else None)
    if not port:
        print('часов не вижу')
        return 1
    print(f'{img} -> {port}')
    try:
        device.flash(img, port, on_line=lambda s: print('  ' + s))
    except device.DeviceError as e:
        print(f'не залилось: {e}')
        return 1
    print('готово, часы перезагрузятся сами')
    return 0


def cmd_backup(args):
    from . import device
    b = _bundle(args)
    out = Path(args.out or (b.dist / 'backup-zeal.bin'))
    out.parent.mkdir(parents=True, exist_ok=True)
    ps = device.ports()
    port = args.port or (ps[0]['port'] if ps else None)
    try:
        device.backup(out, port, on_line=lambda s: print('  ' + s))
    except device.DeviceError as e:
        print(e)
        return 1
    print(f'копия флеша: {out}')
    return 0


def cmd_studio(args):
    from .studio.server import serve
    serve(_bundle(args), port=args.port, open_browser=not args.no_browser)
    return 0


def cmd_emu(args):
    from .emu import emulate
    from .mkmod import MkError
    try:
        return emulate(_bundle(args), args.dir, args.args or [])
    except MkError as e:
        print(e)
        return 1


def cmd_info(args):
    b = _bundle(args)
    prof = b.profile(args.profile)
    ok, got, want = b.base_ok(prof)
    print(f'ZealMod {__version__}')
    print(f'  комплект   {b.root}')
    print(f'  прошивка   {b.base.name} — {"та самая" if ok else "ЧУЖАЯ (" + got[:12] + ")"}')
    print(f'  ядро       {b.core}')
    print(f'  модулей    {len(b.modules())}, тем {len(b.themes())}')
    return 0


# --- разбор командной строки ----------------------------------------------
def main(argv=None):
    ap = argparse.ArgumentParser(prog='zealmod', description='ZealMod: своя прошивка '
                                 'для таймера Zeal')
    ap.add_argument('--bundle', help='каталог комплекта (по умолчанию рядом с программой)')
    ap.add_argument('--profile', default='zeal-v1', help='профиль устройства')
    ap.add_argument('-V', '--version', action='version', version=f'ZealMod {__version__}')
    sub = ap.add_subparsers(dest='cmd', required=True)

    sub.add_parser('devices', help='кто подключён по USB').set_defaults(fn=cmd_devices)

    p = sub.add_parser('mods', help='собрать встроенные приложения в .zm')
    p.add_argument('--only', help='только эти идентификаторы, через запятую')
    p.set_defaults(fn=cmd_mods)

    p = sub.add_parser('build', help='собрать образ прошивки')
    p.add_argument('--all', action='store_true', help='взять все модули из каталога')
    p.add_argument('--mods', help='идентификаторы или пути к .zm, через запятую')
    p.add_argument('--theme', help='файл темы .zt')
    p.add_argument('--set', action='append', help='настройка, например menu_hold_ms=1200')
    p.add_argument('--force', action='store_true', help='собрать даже с чужой прошивкой')
    p.add_argument('-o', '--out', help='куда положить .gbl')
    p.set_defaults(fn=cmd_build)

    p = sub.add_parser('check', help='проверить .zm на совместимость')
    p.add_argument('files', nargs='+')
    p.set_defaults(fn=cmd_check)

    p = sub.add_parser('pack', help='упаковать свою программу в .zm')
    p.add_argument('dir')
    p.add_argument('-o', '--out')
    p.set_defaults(fn=cmd_pack)

    p = sub.add_parser('new', help='заготовка программы или темы')
    p.add_argument('kind', choices=('app', 'theme'))
    p.add_argument('name')
    p.set_defaults(fn=cmd_new)

    p = sub.add_parser('theme', help='упаковать каталог темы в .zt')
    p.add_argument('dir')
    p.add_argument('-o', '--out')
    p.set_defaults(fn=cmd_theme)

    p = sub.add_parser('flash', help='залить образ в часы')
    p.add_argument('image', nargs='?')
    p.add_argument('--port')
    p.set_defaults(fn=cmd_flash)

    p = sub.add_parser('backup', help='снять полную копию флеша')
    p.add_argument('-o', '--out')
    p.add_argument('--port')
    p.set_defaults(fn=cmd_backup)

    p = sub.add_parser('studio', help='графическая программа в браузере')
    p.add_argument('--port', type=int, default=8777)
    p.add_argument('--no-browser', action='store_true')
    p.set_defaults(fn=cmd_studio)

    p = sub.add_parser('emu', help='посмотреть программу на компьютере')
    p.add_argument('dir', nargs='?', help='каталог с module.json (по умолчанию — всё)')
    p.add_argument('args', nargs=argparse.REMAINDER,
                   help='что передать эмулятору (после каталога)')
    p.set_defaults(fn=cmd_emu)

    sub.add_parser('info', help='что лежит в комплекте').set_defaults(fn=cmd_info)

    args = ap.parse_args(argv)
    try:
        return args.fn(args)
    except (BuildError, pack.PackError, FileNotFoundError) as e:
        print(f'ошибка: {e}')
        return 1
    except KeyboardInterrupt:
        return 130


if __name__ == '__main__':
    sys.exit(main())
