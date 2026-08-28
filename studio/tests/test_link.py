#!/usr/bin/env python3
"""Проверка компоновщика: то же самое должен получить GNU ld.

Для каждого модуля из dist/modules мы раскладываем его своими силами, а затем
просим настоящий ld разложить точно так же (тот же порядок секций, те же
адреса, имена ядра — из core.elf) и сравниваем байты.  Расхождение означает
ошибку в relocations — молча она превратилась бы в зависшие часы.

  python3 studio/tests/test_link.py
"""
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'studio'))

from zealmod.elf import Elf32                       # noqa: E402
from zealmod.link import Arena, Linker              # noqa: E402

LD = 'riscv64-elf-ld'
OBJCOPY = 'riscv64-elf-objcopy'
TEXT_BASE = 0x42290000
DATA_BASE = 0x3C300000
BSS_BASE = 0x50001000


def ld_script(places):
    """Скрипт для ld, повторяющий нашу раскладку секция в секцию."""
    out = ['SECTIONS {']
    for name, addr, _size in places:
        out.append(f'  . = {addr:#x};')
        out.append(f'  {name} {"(NOLOAD) " if name.startswith(".bss") else ""}: '
                   f'{{ *({name}) }}')
    out.append('  /DISCARD/ : { *(.comment) *(.riscv.attributes) *(.note*) }')
    out.append('}')
    return '\n'.join(out)


def check(zm: Path, core: Path, tmp: Path):
    obj = tmp / 'code.o'
    with zipfile.ZipFile(zm) as z:
        obj.write_bytes(z.read('code.o'))
    # ld умеет склеивать одинаковые строки в SHF_MERGE-секциях, наш компоновщик
    # переносит секции как есть; чтобы сравнивать байты, склейку выключаем
    for sec in ('.rodata.str1.4', '.rodata.str1.8', '.srodata.cst4', '.srodata.cst8'):
        subprocess.run([OBJCOPY, f'--set-section-flags={sec}=alloc,readonly,contents',
                        str(obj)], capture_output=True)
    core_elf = Elf32(core.read_bytes(), 'core.elf')
    exports = {s.name: s.value for s in core_elf.symbols
               if s.defined and s.global_ and s.name}
    lk = Linker(exports, Arena('text', TEXT_BASE, TEXT_BASE + 0x100000),
                Arena('rodata', DATA_BASE, DATA_BASE + 0x200000),
                Arena('bss', BSS_BASE, BSS_BASE + 0x8000))
    mine = lk.link(Elf32(obj.read_bytes(), zm.stem), zm.stem)

    script = tmp / 'ref.ld'
    script.write_text(ld_script(mine.places))
    ref = tmp / 'ref.elf'
    r = subprocess.run([LD, '-m', 'elf32lriscv', '-R', str(core), '-T', str(script),
                        str(obj), '-o', str(ref)], capture_output=True, text=True)
    if r.returncode:
        return False, r.stderr.strip().splitlines()[-1]

    bad = []
    for name, addr, size in mine.places:
        if name.startswith('.bss') or size == 0:
            continue
        binf = tmp / 'ref.bin'
        subprocess.run([OBJCOPY, '-O', 'binary', f'--only-section={name}',
                        str(ref), str(binf)], check=True)
        want = binf.read_bytes()
        got = b''
        for at, blob in mine.chunks:
            if at == addr:
                got = blob
                break
        if got != want:
            n = min(len(got), len(want))
            first = next((i for i in range(n) if got[i] != want[i]), n)
            bad.append(f'{name}: расходится с {first:#x} '
                       f'(наше {got[first:first+4].hex()}, ld {want[first:first+4].hex()})')
    return not bad, '; '.join(bad)


def main():
    core = ROOT / 'work' / 'build' / 'core.elf'
    if not core.exists():
        print('нет work/build/core.elf — собери: cd work && make core')
        return 1
    mods = sorted((ROOT / 'dist' / 'modules').glob('*.zm'))
    if not mods:
        print('нет модулей — собери: zealmod mods')
        return 1
    rc = 0
    with tempfile.TemporaryDirectory() as td:
        for zm in mods:
            ok, msg = check(zm, core, Path(td))
            print(f'{"ок " if ok else "НЕТ"} {zm.stem:<10} {msg}')
            rc |= 0 if ok else 1
    print('всё сошлось' if not rc else 'есть расхождения')
    return rc


if __name__ == '__main__':
    sys.exit(main())
