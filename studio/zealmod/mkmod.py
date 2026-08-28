"""Сборка .zm из исходников — это делает разработчик, а не пользователь.

Нужен компилятор riscv64-elf-gcc и Pillow (для обложек).  Готовый .zm потом
ставится на любые часы через Studio, где ничего этого нет.
"""
import json
import os
import subprocess
from pathlib import Path

from . import pack, tab

CC = os.environ.get('ZM_CC', 'riscv64-elf-gcc')
LD = os.environ.get('ZM_LD', 'riscv64-elf-ld')

# Ровно те же ключи, что у ядра, минус -flto: имена функций модуля должны
# дожить до компоновки, её делает Studio (link.py).
CFLAGS = ['-march=rv32imc_zicsr', '-mabi=ilp32', '-mno-relax', '-msmall-data-limit=0',
          '-Os', '-ffreestanding', '-nostdlib', '-fno-common', '-fno-strict-aliasing',
          '-Wall', '-Wextra', '-Wno-unused-parameter']


class MkError(Exception):
    pass


def _run(cmd):
    r = subprocess.run(cmd, capture_output=True)
    if r.returncode:
        raise MkError(f'{cmd[0]}:\n' + r.stderr.decode('utf-8', 'replace').strip())
    return r.stdout


def compile_module(spec, fw_root: Path, out_dir: Path, includes=()):
    """Скомпилировать исходники модуля в один перемещаемый объектник."""
    fw_root = Path(fw_root)
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    inc = ['-I' + str(fw_root / p) for p in ('src', 'build')]
    inc += ['-I' + str(fw_root / p) for p in spec.get('include', [])]
    inc += ['-I' + str(p) for p in includes]
    objs = []
    for src in spec['sources']:
        s = fw_root / src
        if not s.exists():
            raise MkError(f'нет исходника {s}')
        o = out_dir / (spec['id'] + '_' + Path(src).name.replace('.', '_') + '.o')
        _run([CC, *CFLAGS, *inc, '-c', str(s), '-o', str(o)])
        objs.append(str(o))
    code = out_dir / (spec['id'] + '.o')
    # ld -r склеивает объекты, оставляя файл перемещаемым
    _run([LD, '-r', '-m', 'elf32lriscv', '-o', str(code), *objs])
    return code.read_bytes()


def cover_from_png(path, size=96):
    """PNG -> (8bpp, палитра, PNG для интерфейса)."""
    try:
        from PIL import Image
    except ImportError:
        raise MkError('для обложек нужен Pillow: pip install pillow') from None
    import io
    im = Image.open(path).convert('RGB')
    if im.size != (size, size):
        im = im.resize((size, size), Image.NEAREST if im.width % size == 0 else Image.LANCZOS)
    q = im.convert('P', palette=Image.ADAPTIVE, colors=256)
    pal = (q.getpalette() + [0] * 768)[:768]
    idx = bytes(q.getdata())
    raw = b''.join(int.to_bytes(tab.rgb565(*pal[i * 3:i * 3 + 3]), 2, 'little')
                   for i in range(256))
    buf = io.BytesIO()
    q.convert('RGB').save(buf, 'PNG')
    return idx, raw, buf.getvalue()


def build_module(spec, fw_root: Path, out_dir: Path, cover_dir: Path, work: Path,
                 includes=()):
    """Собрать и упаковать один модуль. Возвращает путь к .zm."""
    obj = compile_module(spec, fw_root, work, includes)
    cover = pal = png = b''
    cov = spec.get('cover')
    if cov:
        tries = [Path(cov), Path(fw_root) / cov, Path(cover_dir) / cov,
                 Path(cover_dir) / f'{cov}.png']
        p = next((t for t in tries if t.is_file()), None)
        if p:
            cover, pal, png = cover_from_png(p)
        else:
            print(f'  ! обложки {cov} нет — модуль будет без картинки')
    man = {k: v for k, v in spec.items()
           if k in ('id', 'name', 'version', 'author', 'entry', 'exit_button',
                    'exit_hold', 'description', 'kind', 'abi')}
    man.setdefault('version', '1.0')
    man.setdefault('author', 'ZealMod')
    man.setdefault('abi', tab.ABI)
    out = Path(out_dir) / f"{spec['id']}.zm"
    out.parent.mkdir(parents=True, exist_ok=True)
    pack.write_module(out, man, obj, cover=cover, pal=pal, cover_png=png,
                      blobs=_blobs(spec, fw_root))
    return out


def _blobs(spec, fw_root):
    out = {}
    for name, path in (spec.get('blobs') or {}).items():
        p = fw_root / path
        if not p.exists():
            raise MkError(f'нет данных {p}')
        out[name] = p.read_bytes()
    return out


def load_specs(path):
    data = json.loads(Path(path).read_text('utf-8'))
    return data['modules'] if isinstance(data, dict) else data
