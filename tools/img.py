#!/usr/bin/env python3
"""Zeal firmware image helper: vaddr<->file offset, hexdump, disasm, patching."""
import struct, subprocess, sys, os

IMG = os.path.join(os.path.dirname(__file__), '..', '..', 'original.gbl')

def segments(d):
    segs = []
    n = d[1]; off = 24
    for _ in range(n):
        va, ln = struct.unpack('<II', d[off:off+8]); off += 8
        segs.append((va, ln, off)); off += ln
    return segs

class Img:
    def __init__(self, path=IMG):
        self.path = path
        self.d = bytearray(open(path, 'rb').read())
        self.segs = segments(self.d)
    def off(self, va):
        for sva, ln, o in self.segs:
            if sva <= va < sva + ln:
                return o + (va - sva)
        raise KeyError(f'{va:#x} not mapped')
    def read(self, va, n):
        o = self.off(va); return bytes(self.d[o:o+n])
    def write(self, va, b):
        o = self.off(va); self.d[o:o+len(b)] = b

PART = 0x100000            # ota_0 начинается здесь, значит страницы флеша считаются отсюда
MMU_TABLE = 0x600C5000     # таблица MMU: одна на шину команд и на шину данных
MMU_PAGE = 0x10000
CODE_ENTRY = 40            # свободная запись под наш второй кусок кода


def layout(im=None):
    """Куда лечь моду.

    У ESP32-C3 таблица MMU общая: номер записи = (адрес & 0x7FFFFF) >> 16, и
    неважно, читают по нему команды (0x42......) или данные (0x3C......).  У
    стоковой прошивки записи 0..6 заняты кодом, 7..25 — данными, поэтому наш
    код может расти только до первой страницы данных — это всего 47 КБ.

    Что не влезло, кладём внутрь сегмента данных на границу страницы флеша и
    на старте отображаем ту же страницу ещё раз — в свободную запись MMU, уже
    как код.  Так мод получает ещё 64 КБ, ничего не ломая.
    """
    if im is None:
        im = Img()
    drom = next(s for s in im.segs if 0x3C000000 <= s[0] < 0x3D000000)
    irom = next(s for s in im.segs if 0x42000000 <= s[0] < 0x43000000)
    flash = lambda va: PART + drom[2] + (va - drom[0])
    text = (irom[0] + irom[1] + 15) & ~15
    limit = 0x42000000 + ((drom[0] & 0x7FFFFF) & ~(MMU_PAGE - 1))
    end = (drom[0] + drom[1] + 15) & ~15
    pt2 = end + (-flash(end)) % MMU_PAGE
    return dict(text=text, text_len=limit - text,
                text2=0x42000000 + CODE_ENTRY * MMU_PAGE,
                entry=CODE_ENTRY, page=flash(pt2) // MMU_PAGE,
                pt2_lma=pt2, data=pt2 + MMU_PAGE, drom=drom, irom=irom)


def disasm(data, va):
    import tempfile
    with tempfile.NamedTemporaryFile(suffix='.bin', delete=False) as f:
        f.write(data); p = f.name
    out = subprocess.run(['riscv64-elf-objdump', '-D', '-b', 'binary', '-m', 'riscv:rv32',
                          '-M', 'numeric', f'--adjust-vma={va:#x}', p],
                         capture_output=True, text=True).stdout
    os.unlink(p)
    return '\n'.join(l for l in out.splitlines() if '\t' in l)

if __name__ == '__main__':
    img = Img()
    cmd = sys.argv[1]
    va = int(sys.argv[2], 0); n = int(sys.argv[3], 0) if len(sys.argv) > 3 else 32
    if cmd == 'd':
        print(disasm(img.read(va, n), va))
    elif cmd == 'x':
        b = img.read(va, n)
        for i in range(0, len(b), 16):
            print(f'{va+i:#010x}  ' + ' '.join(f'{c:02x}' for c in b[i:i+16]) +
                  '  ' + ''.join(chr(c) if 32 <= c < 127 else '.' for c in b[i:i+16]))
    elif cmd == 'o':
        print(f'{img.off(va):#x}')
