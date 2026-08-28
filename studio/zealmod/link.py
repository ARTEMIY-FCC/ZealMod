"""Компоновщик модулей ZealMod.

Модуль (.zm) везёт в себе перемещаемый объектный файл: ни адресов, ни
разрешённых внешних имён.  Studio раскладывает его секции по свободным адресам
образа, разрешает внешние имена по таблице ABI ядра и правит инструкции.
Компилятора на машине пользователя не требуется.
"""
from dataclasses import dataclass, field

from . import reloc
from .elf import Elf32, SHN_ABS, SHN_COMMON, SHN_UNDEF, STT_SECTION, SHF_WRITE

TEXT, RODATA, BSS = 'text', 'rodata', 'bss'


class LinkError(Exception):
    pass


class Arena:
    """Кусок адресного пространства, раздаваемый по порядку."""

    def __init__(self, name, base, limit):
        self.name = name
        self.base = base
        self.at = base
        self.limit = limit

    def take(self, size, align=4):
        self.at = (self.at + align - 1) & ~(align - 1)
        at = self.at
        if at + size > self.limit:
            raise LinkError(f'{self.name}: out of space ({size} B needed, '
                            f'{max(0, self.limit - at)} B left)')
        self.at += size
        return at

    @property
    def used(self):
        return self.at - self.base

    @property
    def left(self):
        return self.limit - self.at


@dataclass
class Linked:
    """Результат размещения одного объектного файла."""
    name: str
    symbols: dict = field(default_factory=dict)     # имя -> адрес
    chunks: list = field(default_factory=list)      # (адрес, байты) для записи в образ
    bss: tuple = (0, 0)                             # (начало, конец)
    text_bytes: int = 0
    rodata_bytes: int = 0
    bss_bytes: int = 0
    imports: set = field(default_factory=set)       # какие имена ядра понадобились
    places: list = field(default_factory=list)      # (секция, адрес, размер)


class Linker:
    def __init__(self, exports: dict, text: Arena, rodata: Arena, bss: Arena):
        self.exports = exports
        self.arenas = {TEXT: text, RODATA: rodata, BSS: bss}

    # --- куда какую секцию ------------------------------------------------
    @staticmethod
    def region(sec):
        if sec.nobits:
            return BSS
        if sec.exec:
            return TEXT
        if sec.flags & SHF_WRITE:
            return None          # изменяемые данные — их некому копировать
        return RODATA

    def link(self, elf: Elf32, name=None, extern=None) -> Linked:
        """extern — дополнительные имена (например, блобы модуля)."""
        name = name or elf.name
        out = Linked(name=name)
        extern = extern or {}
        placed = {}                                   # индекс секции -> адрес

        secs = [s for s in elf.sections if s.alloc and s.size]
        secs.sort(key=lambda s: (0 if s.name.startswith('.text.hooks') else
                                 1 if s.exec else 2 if not s.nobits else 3, s.idx))
        for s in secs:
            r = self.region(s)
            if r is None:
                raise LinkError(f'{name}: section {s.name} ({s.size} B) is writable — initialised '
                                f'mutable data is not supported, nothing copies it to RAM')
            placed[s.idx] = self.arenas[r].take(s.size, max(s.align, 4))
            out.places.append((s.name, placed[s.idx], s.size))
            if r == TEXT:
                out.text_bytes += s.size
            elif r == RODATA:
                out.rodata_bytes += s.size
            else:
                out.bss_bytes += s.size

        bss_addrs = [placed[s.idx] for s in secs if s.nobits]
        if bss_addrs:
            lo = min(bss_addrs)
            hi = max(placed[s.idx] + s.size for s in secs if s.nobits)
            out.bss = (lo, hi)

        # --- символы -------------------------------------------------------
        local = {}
        for sym in elf.symbols:
            if sym.shndx == SHN_ABS or not sym.defined:
                continue
            if sym.shndx == SHN_COMMON:
                raise LinkError(f'{name}: symbol {sym.name} is COMMON — build with -fno-common')
            if sym.shndx >= len(elf.sections):
                continue
            base = placed.get(sym.shndx)
            if base is None:
                continue                       # символ в невыгружаемой секции
            addr = base + sym.value
            if sym.name:
                local[sym.name] = addr
                if sym.global_:
                    out.symbols[sym.name] = addr

        missing = []

        def resolve(idx):
            sym = elf.symbols[idx]
            if sym.shndx == SHN_ABS:
                return sym.value
            if sym.defined:
                if sym.type == STT_SECTION:
                    a = placed.get(sym.shndx)
                    if a is None:
                        raise LinkError(f'{name}: reference to a non-allocated section '
                                        f'{elf.sections[sym.shndx].name}')
                    return a
                a = placed.get(sym.shndx)
                if a is None:
                    raise LinkError(f'{name}: symbol {sym.name} lives in a non-allocated section')
                return a + sym.value
            # внешний
            nm = sym.name
            if nm in extern:
                return extern[nm]
            if nm in self.exports:
                out.imports.add(nm)
                return self.exports[nm]
            if nm not in missing:
                missing.append(nm)
            return 0

        # --- релокации -----------------------------------------------------
        pcrel = {}
        bodies = []
        for s in secs:
            if s.nobits:
                continue
            buf = bytearray(s.data)
            rels = elf.relocs(s)
            if rels:
                reloc.apply(buf, placed[s.idx], rels, resolve, pcrel,
                            where=f'{name}:{s.name}')
            bodies.append((placed[s.idx], bytes(buf)))

        if missing:
            raise LinkError(f'{name}: the core does not export: ' + ', '.join(sorted(missing)))
        out.chunks = bodies
        return out


def exports_from_elf(elf: Elf32, allow=None) -> dict:
    """Таблица ABI ядра: имя -> адрес. allow ограничивает список."""
    out = {}
    for s in elf.symbols:
        if not s.defined or not s.name or s.shndx == SHN_UNDEF:
            continue
        if not s.global_:
            continue
        if allow is not None and s.name not in allow:
            continue
        out[s.name] = s.value if s.shndx == SHN_ABS else s.value
    return out
