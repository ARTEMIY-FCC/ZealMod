#!/usr/bin/env python3
"""Собирает прошивку: .ptext в конец IROM, .ptext2 и .prodata в конец DROM,
три хука по 4 байта, пересчёт контрольной суммы и SHA-256.

Про .ptext2 и вторую страницу MMU — см. img.layout().

  python3 tools/patch.py build/payload.elf build/zeal-mod.gbl
"""
import hashlib, os, struct, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from img import Img, layout

HOOKS = [  # (адрес пролога, символ трамплина, ожидаемые затираемые байты)
    (0x42004C76, 'zg_hook_boot',  bytes.fromhex('3971 26da')[:4]),
    (0x42005D96, 'zg_hook_flush', bytes.fromhex('ae87 3287')[:4]),
    (0x42005426, 'zg_hook_keys',  bytes.fromhex('03470900')[:4]),
]
PART_SIZE = 2 * 1024 * 1024


def sh(*a):
    r = subprocess.run(a, capture_output=True)
    if r.returncode:
        sys.exit(f'{a[0]} failed:\n{r.stderr.decode()}')
    return r.stdout


def elf_sections(elf):
    out = sh('riscv64-elf-readelf', '-S', '-W', elf).decode()
    secs = {}
    for line in out.splitlines():
        line = line.strip()
        if not line.startswith('['):
            continue
        parts = line.split(']', 1)[1].split()
        if len(parts) < 5:
            continue
        name, typ, addr, off, size = parts[0], parts[1], parts[2], parts[3], parts[4]
        try:
            secs[name] = (int(addr, 16), int(off, 16), int(size, 16))
        except ValueError:
            pass
    return secs


def elf_symbol(elf, name):
    for line in sh('riscv64-elf-nm', elf).decode().splitlines():
        p = line.split()
        if len(p) == 3 and p[2] == name:
            return int(p[0], 16)
    sys.exit(f'нет символа {name}')


def jal_zero(src, dst):
    off = dst - src
    if not (-(1 << 20) <= off < (1 << 20)):
        sys.exit(f'j {src:#x} -> {dst:#x}: не дотягивается ({off:#x})')
    o = off & 0x1FFFFF
    ins = (((o >> 20) & 1) << 31 | ((o >> 1) & 0x3FF) << 21 | ((o >> 11) & 1) << 20 |
           ((o >> 12) & 0xFF) << 12 | 0x6F)
    return struct.pack('<I', ins)


def rebuild(segs_data, header, seg_hdrs):
    """header: 24 байта; seg_hdrs: [(vaddr, data)] в исходном порядке"""
    out = bytearray(header)
    csum = 0xEF
    for va, data in seg_hdrs:
        out += struct.pack('<II', va, len(data))
        out += data
        for b in data:
            csum ^= b
    while (len(out) + 1) % 16:
        out += b'\0'
    out.append(csum & 0xFF)
    out += hashlib.sha256(bytes(out)).digest()
    return bytes(out)


def selftest():
    im = Img()
    hdr = bytes(im.d[:24])
    segs = [(va, bytes(im.d[o:o + ln])) for va, ln, o in im.segs]
    if rebuild(im.d, hdr, segs) != bytes(im.d):
        sys.exit('самопроверка: пересборка исходного образа не совпала')
    print('самопроверка пересборки: ок')


def main():
    elf, out = sys.argv[1], sys.argv[2]
    selftest()
    im = Img()
    secs = elf_sections(elf)
    for bad in ('.data', '.sdata'):
        if secs.get(bad, (0, 0, 0))[2]:
            sys.exit(f'{bad} не пуст ({secs[bad][2]} Б): инициализированных изменяемых '
                     f'данных быть не должно — их некому копировать')
    blob = {}
    for name in ('.ptext', '.ptext2', '.prodata'):
        f = f'build/{name.strip(".")}.bin'
        sh('riscv64-elf-objcopy', '-O', 'binary', f'--only-section={name}', elf, f)
        blob[name] = open(f, 'rb').read()

    drom = next(i for i, s in enumerate(im.segs) if 0x3C000000 <= s[0] < 0x3D000000)
    irom = next(i for i, s in enumerate(im.segs) if 0x42000000 <= s[0] < 0x43000000)
    segs = [[va, bytearray(im.d[o:o + ln])] for va, ln, o in im.segs]
    L = layout(im)

    for name, want in (('.ptext', L['text']), ('.ptext2', L['text2']), ('.prodata', L['data'])):
        got = secs.get(name, (0, 0, 0))[0]
        if blob[name] and got != want:
            sys.exit(f'{name} слинкован на {got:#x}, ждали {want:#x} — перегенерируй ld')
    if len(blob['.ptext']) > L['text_len']:
        sys.exit(f'.ptext {len(blob[".ptext"])} Б, а до страницы данных всего {L["text_len"]}')
    if len(blob['.ptext2']) > 0x10000:
        sys.exit(f'.ptext2 {len(blob[".ptext2"])} Б — в одну страницу MMU не влезает')

    segs[irom][1] += b'\0' * ((16 - len(segs[irom][1]) % 16) % 16)
    segs[irom][1] += blob['.ptext']
    segs[irom][1] += b'\0' * ((4 - len(segs[irom][1]) % 4) % 4)
    # второй кусок кода лежит в данных, ровно на границе страницы флеша 
    pad2 = L['pt2_lma'] - segs[drom][0] - len(segs[drom][1])
    if pad2 < 0:
        sys.exit('сегмент данных перерос место под .ptext2')
    segs[drom][1] += b'\0' * pad2
    segs[drom][1] += blob['.ptext2']
    segs[drom][1] += b'\0' * (0x10000 - len(blob['.ptext2']))
    segs[drom][1] += blob['.prodata']
    # рост DROM обязан быть кратен 64 КБ, иначе IROM теряет конгруэнтность
    grow = len(segs[drom][1]) - im.segs[drom][1]
    pad = (-grow) % 0x10000
    segs[drom][1] += b'\0' * pad

    # хуки
    def poke(va, data):
        for i, (sva, s) in enumerate(segs):
            ln = len(s)
            if sva <= va < sva + ln:
                s[va - sva:va - sva + len(data)] = data
                return
        sys.exit(f'{va:#x} вне сегментов')

    import os as _os
    only = _os.environ.get('ONLY_HOOK')
    if _os.environ.get('NO_HOOKS'):
        HOOKS_LOCAL = []
    elif only is not None:
        HOOKS_LOCAL = [HOOKS[int(i)] for i in only.split(',')]
    else:
        HOOKS_LOCAL = HOOKS
    for va, sym, _ in HOOKS_LOCAL:
        target = elf_symbol(elf, sym)
        poke(va, jal_zero(va, target))
        print(f'хук {va:#010x} -> {sym} {target:#010x}')

    for va, dd in segs:            # бутлоадер: длина каждого сегмента кратна 4
        if len(dd) % 4:
            sys.exit(f'сегмент {va:#x}: длина {len(dd)} не делится на 4')
    image = rebuild(im.d, bytes(im.d[:24]), [(va, bytes(d)) for va, d in segs])
    if len(image) > PART_SIZE:
        sys.exit(f'образ {len(image)} Б не влезает в ota_0 ({PART_SIZE})')
    open(out, 'wb').write(image)
    print(f'.ptext   {len(blob[".ptext"]):7d} Б @ {L["text"]:#x} (место до данных {L["text_len"]})')
    print(f'.ptext2  {len(blob[".ptext2"]):7d} Б @ {L["text2"]:#x} (лежит по {L["pt2_lma"]:#x})')
    print(f'.prodata {len(blob[".prodata"]):7d} Б @ {L["data"]:#x} (+{pad} выравнивание)')
    print(f'{out}: {len(image)} Б, свободно в разделе {PART_SIZE - len(image)} Б')


main()
