"""Образ приложения ESP32-C3 (.gbl у Zeal — обычный esp-idf app image).

Формат: 24 байта заголовка, дальше сегменты (vaddr, длина, данные), в хвосте
выравнивание до 16, контрольный байт XOR и SHA-256 всего предыдущего.

Правила, за которые ругается бутлоадер (проверено на живом железе):
  * длина каждого сегмента кратна 4;
  * у отображаемых через кэш сегментов (DROM/IROM) файловое смещение обязано
    быть сравнимо с виртуальным адресом по модулю 64 КБ — поэтому рост DROM
    добивается нулями до кратности 0x10000.
"""
import hashlib
import struct

DROM = 'DROM'
IROM = 'IROM'
PART_SIZE = 2 * 1024 * 1024          # ota_0


def kind_of(va):
    if 0x3C000000 <= va < 0x3D000000:
        return DROM
    if 0x42000000 <= va < 0x43000000:
        return IROM
    if 0x4037C000 <= va < 0x403E0000:
        return 'IRAM'
    if 0x3FC80000 <= va < 0x3FD00000:
        return 'DRAM'
    return 'RTC'


class ImageError(Exception):
    pass


class AppImage:
    def __init__(self, data: bytes):
        d = bytes(data)
        if not d or d[0] != 0xE9:
            raise ImageError('не образ приложения ESP32 (нет 0xE9 в начале)')
        self.header = bytearray(d[:24])
        n = d[1]
        segs, off = [], 24
        for _ in range(n):
            va, ln = struct.unpack_from('<II', d, off)
            off += 8
            segs.append([va, bytearray(d[off:off + ln])])
            off += ln
        self.segs = segs
        self.orig_len = {i: len(s[1]) for i, s in enumerate(segs)}
        self.drom = self._find(DROM)
        self.irom = self._find(IROM)

    # ---- поиск и доступ ---------------------------------------------------
    def _find(self, kind):
        for i, (va, _) in enumerate(self.segs):
            if kind_of(va) == kind:
                return i
        raise ImageError(f'в образе нет сегмента {kind}')

    def end(self, kind):
        """Первый свободный адрес за сегментом, выровненный на 16."""
        i = self.drom if kind == DROM else self.irom
        va, d = self.segs[i]
        return (va + len(d) + 15) & ~15

    def read(self, va, n):
        for sva, d in self.segs:
            if sva <= va < sva + len(d):
                o = va - sva
                if o + n > len(d):
                    raise ImageError(f'чтение {va:#x}+{n} вылезает за сегмент')
                return bytes(d[o:o + n])
        raise ImageError(f'{va:#x} не отображён')

    def write(self, va, data):
        for sva, d in self.segs:
            if sva <= va < sva + len(d):
                o = va - sva
                if o + len(data) > len(d):
                    raise ImageError(f'запись {va:#x}+{len(data)} вылезает за сегмент')
                d[o:o + len(data)] = data
                return
        raise ImageError(f'{va:#x} не отображён')

    def append(self, kind, blob, align=16):
        """Дописать блок в конец IROM/DROM. Возвращает виртуальный адрес."""
        i = self.drom if kind == DROM else self.irom
        va, d = self.segs[i]
        pad = (-(va + len(d))) % align
        d += b'\0' * pad
        at = va + len(d)
        d += blob
        return at

    def pad_tail(self):
        """Выравнивание, которого требует бутлоадер."""
        _, irom = self.segs[self.irom]
        irom += b'\0' * ((-len(irom)) % 4)
        dva, drom = self.segs[self.drom]
        grow = len(drom) - self.orig_len[self.drom]
        if grow:
            drom += b'\0' * ((-grow) % 0x10000)

    # ---- сборка -----------------------------------------------------------
    def build(self) -> bytes:
        self.pad_tail()
        out = bytearray(self.header)
        csum = 0xEF
        for va, d in self.segs:
            if len(d) % 4:
                raise ImageError(f'сегмент {va:#x}: длина {len(d)} не делится на 4')
            out += struct.pack('<II', va, len(d))
            out += d
            for b in d:
                csum ^= b
        while (len(out) + 1) % 16:
            out += b'\0'
        out.append(csum & 0xFF)
        out += hashlib.sha256(bytes(out)).digest()
        # проверка конгруэнтности — ровно то, что делает кэш при загрузке
        off = 24
        for va, d in self.segs:
            off += 8
            if kind_of(va) in (DROM, IROM) and (off - va) & 0xFFFF:
                raise ImageError(f'сегмент {va:#x} не конгруэнтен: кэш отобразит мусор')
            off += len(d)
        if len(out) > PART_SIZE:
            raise ImageError(f'образ {len(out)} Б не влезает в раздел ota_0 ({PART_SIZE} Б)')
        return bytes(out)

    def free_bytes(self):
        """Сколько ещё влезет в раздел (с учётом будущего выравнивания)."""
        size = 24 + 33 + sum(8 + len(d) for _, d in self.segs)
        return PART_SIZE - size


def jal(src, dst):
    """Безусловный переход `j dst`, 4 байта, — так ставятся хуки."""
    off = dst - src
    if off & 1 or not -(1 << 20) <= off < (1 << 20):
        raise ImageError(f'j {src:#x} -> {dst:#x}: не дотягивается ({off:#x})')
    o = off & 0x1FFFFF
    ins = (((o >> 20) & 1) << 31 | ((o >> 1) & 0x3FF) << 21 | ((o >> 11) & 1) << 20 |
           ((o >> 12) & 0xFF) << 12 | 0x6F)
    return struct.pack('<I', ins)
