#!/usr/bin/env python3
"""Проверка собранного образа так, как его проверяет сам ESP-IDF."""
import hashlib, struct, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from img import Img, disasm

f = sys.argv[1] if len(sys.argv) > 1 else 'build/zeal-mod.gbl'
d = open(f, 'rb').read()
assert d[0] == 0xE9, 'нет magic 0xE9'
n = d[1]
off, csum = 24, 0xEF
print(f'{f}: {len(d)} байт, сегментов {n}, entry {struct.unpack("<I", d[4:8])[0]:#x}')
for i in range(n):
    va, ln = struct.unpack('<II', d[off:off + 8]); off += 8
    cong = (off - va) & 0xFFFF
    kind = ('DROM' if 0x3C000000 <= va < 0x3D000000 else 'IROM' if 0x42000000 <= va < 0x43000000
            else 'IRAM' if 0x4037C000 <= va < 0x403E0000 else 'DRAM' if 0x3FC80000 <= va < 0x3FD00000
            else 'RTC')
    bad = ''
    if kind in ('DROM', 'IROM') and cong:
        bad = '  <-- НЕ КОНГРУЭНТЕН, кэш отобразит мусор'
    if ln % 4:
        bad += '  <-- длина не кратна 4, бутлоадер отвергнет'
    print(f'  seg{i} {kind} vaddr={va:#010x} len={ln:7d} file={off:#08x} cong={cong:#x}{bad}')
    for b in d[off:off + ln]:
        csum ^= b
    off += ln
pad = off
while (pad + 1) % 16:
    pad += 1
print(f'  контрольная сумма: {"ок" if d[pad] == (csum & 0xFF) else "НЕ СОШЛАСЬ"}')
sha = hashlib.sha256(d[:pad + 1]).digest()
print(f'  sha-256:           {"ок" if sha == d[pad + 1:pad + 33] else "НЕ СОШЛАСЬ"}')
print(f'  хвост:             {len(d) - (pad + 33)} лишних байт')

im = Img(f)
print('\nхук 1 (TDisplay_task):')
print(disasm(im.read(0x42004C70, 16), 0x42004C70))
print('\nхук 2 (flush LVGL):')
print(disasm(im.read(0x42005D90, 16), 0x42005D90))
print('\nтрамплины:')
print(disasm(im.read(0x420647D0, 96), 0x420647D0))
