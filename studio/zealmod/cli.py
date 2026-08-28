"""ZealMod command line.

  zealmod devices                 what is connected over USB
  zealmod mods                    build the built-in programs into .zm files
  zealmod build --all -o out.gbl  compose an image
  zealmod check game.zm           is this program compatible with the watch
  zealmod pack folder/            pack your own program into a .zm
  zealmod new app mygame          scaffold a new program
  zealmod flash out.gbl           write the image to the watch
  zealmod studio                  the graphical app, in your browser
"""
import argparse
import json
import sys
from pathlib import Path

from . import __version__, pack, tab
from .build import BuildError, build
from .res import Bundle


def human(n):
    return f'{n/1024:.1f} KB' if n >= 1024 else f'{n} B'


def _bundle(args):
    return Bundle(getattr(args, 'bundle', None))


# --- команды ---------------------------------------------------------------
def cmd_devices(args):
    from . import device
    ps = device.ports()
    if not ps:
        print('no timer in sight. Use a cable with data wires and try again.')
        return 1
    for p in ps:
        mark = '*' if p['score'] else ' '
        print(f'{mark} {p["port"]:<28} {p["vid"]}:{p["pid"]} {p["desc"]}')
    if not device.esptool_ok():
        print('\n! flashing needs esptool: pip install esptool')
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
            print(f'{td.parent.name}: theme failed — {e}')
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
    print(f'done: {done} modules')
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
            raise BuildError(f'module {n} not found')
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
        print('no modules: build them with `zealmod mods`')
        return 1
    mods = _load_modules(b, names)
    cfg = {'lang': args.lang}
    theme, assets, _src = (tab.DEFAULT_THEME, {}, {})
    if args.theme:
        theme, assets, _src = pack.read_theme(args.theme, args.lang)
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
        print(f'! the stock image is not the one this mod was built against\n'
              f'  have {got}\n  want {want}\n  (--force if you are sure)')
        return 1
    try:
        image, rep = build(b.base.read_bytes(), b.core.read_bytes(), mods, theme=theme,
                           cfg=cfg, profile=prof, theme_assets=assets)
    except BuildError as e:
        print(f'build failed: {e}')
        return 1
    out = Path(args.out or (b.dist / 'zealmod.gbl'))
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(image)
    for m in rep.modules:
        print(f'  {m["id"]:<10} code {human(m["code"]):>10}  data {human(m["data"]):>10}'
              f'  RTC {m["bss"]} B')
    print(f'{out}: {human(rep.size)}, {human(rep.free)} free; '
          f'{rep.pages} code pages, {rep.bss} B of RTC for modules')
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
        print(f'{m.id} "{m.title}" {man.get("version", "")} — '
              f'{"compatible" if r["ok"] else "NOT COMPATIBLE"}')
        print(f'  code {human(r["code"])}, data {human(r["data"])}, RTC {r["bss"]} B, '
              f'{len(r["imports"])} calls into the core')
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
        print(f'{manf} not found')
        return 1
    if not b.sdk.exists():
        print(f'no SDK headers at {b.sdk}\n'
              f'  run pack inside a ZealMod kit, or point at one:\n'
              f'  zealmod --bundle /path/to/zealmod pack {args.dir}')
        return 1
    spec = json.loads(manf.read_text('utf-8'))
    # без комплекта под рукой (например, установленный zealmod вне репозитория)
    # кладём .zm рядом с исходниками
    out = Path(args.out or (b.dist / 'modules' if b.dist.exists() else src))
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
    print(f'created: {p}')
    print(f'next:  zealmod {"pack" if args.kind == "app" else "theme"} {p}')
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
        print(f'no image at {img}')
        return 1
    ps = device.ports()
    port = args.port or (ps[0]['port'] if ps else None)
    if not port:
        print('no timer in sight')
        return 1
    print(f'{img} -> {port}')
    try:
        device.flash(img, port, on_line=lambda s: print('  ' + s))
    except device.DeviceError as e:
        print(f'flashing failed: {e}')
        return 1
    print('done, the watch reboots on its own')
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
    print(f'flash backup: {out}')
    return 0


def cmd_studio(args):
    from .studio.server import serve
    return serve(_bundle(args), port=args.port, open_browser=not args.no_browser) or 0


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
    print(f'  kit       {b.root}')
    print(f'  firmware  {b.base.name} — {"the expected one" if ok else "FOREIGN (" + got[:12] + ")"}')
    print(f'  core      {b.core}')
    print(f'  modules   {len(b.modules())}, themes {len(b.themes())}')
    return 0


# --- разбор командной строки ----------------------------------------------
def main(argv=None):
    ap = argparse.ArgumentParser(prog='zealmod',
                                 description='ZealMod: your own firmware for the Zeal timer')
    ap.add_argument('--bundle', help='kit directory (defaults to the one next to this script)')
    ap.add_argument('--profile', default='zeal-v1', help='device profile')
    ap.add_argument('-V', '--version', action='version', version=f'ZealMod {__version__}')
    sub = ap.add_subparsers(dest='cmd', required=True)

    sub.add_parser('devices', help='what is connected over USB').set_defaults(fn=cmd_devices)

    p = sub.add_parser('mods', help='build the built-in programs into .zm files')
    p.add_argument('--only', help='only these ids, comma separated')
    p.set_defaults(fn=cmd_mods)

    p = sub.add_parser('build', help='compose a firmware image')
    p.add_argument('--all', action='store_true', help='take every module in the kit')
    p.add_argument('--mods', help='ids or paths to .zm files, comma separated')
    p.add_argument('--theme', help='a .zt theme file')
    p.add_argument('--lang', default='en', choices=('en', 'ru'),
                   help='language of the firmware texts (default en)')
    p.add_argument('--set', action='append', help='a setting, e.g. menu_hold_ms=1200')
    p.add_argument('--force', action='store_true', help='build even against a foreign firmware')
    p.add_argument('-o', '--out', help='where to write the .gbl')
    p.set_defaults(fn=cmd_build)

    p = sub.add_parser('check', help='check a .zm for compatibility')
    p.add_argument('files', nargs='+')
    p.set_defaults(fn=cmd_check)

    p = sub.add_parser('pack', help='pack your own program into a .zm')
    p.add_argument('dir')
    p.add_argument('-o', '--out')
    p.set_defaults(fn=cmd_pack)

    p = sub.add_parser('new', help='scaffold a program or a theme')
    p.add_argument('kind', choices=('app', 'theme'))
    p.add_argument('name')
    p.set_defaults(fn=cmd_new)

    p = sub.add_parser('theme', help='pack a theme directory into a .zt')
    p.add_argument('dir')
    p.add_argument('-o', '--out')
    p.set_defaults(fn=cmd_theme)

    p = sub.add_parser('flash', help='write an image to the watch')
    p.add_argument('image', nargs='?')
    p.add_argument('--port')
    p.set_defaults(fn=cmd_flash)

    p = sub.add_parser('backup', help='read the whole flash into a file')
    p.add_argument('-o', '--out')
    p.add_argument('--port')
    p.set_defaults(fn=cmd_backup)

    p = sub.add_parser('studio', help='the graphical app, in your browser')
    p.add_argument('--port', type=int, default=8777)
    p.add_argument('--no-browser', action='store_true')
    p.set_defaults(fn=cmd_studio)

    p = sub.add_parser('emu', help='run a program on your computer')
    p.add_argument('dir', nargs='?', help='a directory with module.json (default: everything)')
    p.add_argument('args', nargs=argparse.REMAINDER,
                   help='arguments for the emulator (after the directory)')
    p.set_defaults(fn=cmd_emu)

    sub.add_parser('info', help='what the kit contains').set_defaults(fn=cmd_info)

    args = ap.parse_args(argv)
    try:
        return args.fn(args)
    except (BuildError, pack.PackError, FileNotFoundError) as e:
        print(f'error: {e}')
        return 1
    except KeyboardInterrupt:
        return 130


if __name__ == '__main__':
    sys.exit(main())
