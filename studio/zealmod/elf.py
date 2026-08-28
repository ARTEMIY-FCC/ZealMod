"""Чтение ELF32 (RISC-V) без внешних зависимостей.

Studio должен уметь собирать прошивку на машине, где нет ни компилятора, ни
readelf: модули приходят готовыми объектными файлами внутри .zm, а размещает и
связывает их наш собственный компоновщик (link.py).
"""
import struct
from dataclasses import dataclass, field

# --- типы секций -----------------------------------------------------------
SHT_PROGBITS, SHT_SYMTAB, SHT_STRTAB, SHT_RELA = 1, 2, 3, 4
SHT_NOBITS, SHT_REL, SHT_NOTE = 8, 9, 7
SHF_WRITE, SHF_ALLOC, SHF_EXECINSTR = 0x1, 0x2, 0x4
SHN_UNDEF, SHN_ABS, SHN_COMMON = 0, 0xFFF1, 0xFFF2

STT_NOTYPE, STT_OBJECT, STT_FUNC, STT_SECTION, STT_FILE = 0, 1, 2, 3, 4
STB_LOCAL, STB_GLOBAL, STB_WEAK = 0, 1, 2


@dataclass
class Section:
    idx: int
    name: str
    type: int
    flags: int
    addr: int
    offset: int
    size: int
    link: int
    info: int
    align: int
    entsize: int
    data: bytes = b''

    @property
    def alloc(self):
        return bool(self.flags & SHF_ALLOC)

    @property
    def exec(self):
        return bool(self.flags & SHF_EXECINSTR)

    @property
    def nobits(self):
        return self.type == SHT_NOBITS


@dataclass
class Symbol:
    idx: int
    name: str
    value: int
    size: int
    info: int
    other: int
    shndx: int

    @property
    def bind(self):
        return self.info >> 4

    @property
    def type(self):
        return self.info & 0xF

    @property
    def defined(self):
        return self.shndx != SHN_UNDEF

    @property
    def global_(self):
        return self.bind in (STB_GLOBAL, STB_WEAK)


@dataclass
class Reloc:
    where: int      # смещение внутри секции
    sym: int        # индекс символа
    type: int
    addend: int


class Elf32:
    """Ровно столько ELF, сколько нужно компоновщику."""

    def __init__(self, data: bytes, name='<mem>'):
        self.name = name
        self.d = data
        if data[:4] != b'\x7fELF':
            raise ValueError(f'{name}: не ELF')
        if data[4] != 1 or data[5] != 1:
            raise ValueError(f'{name}: нужен 32-битный little-endian ELF')
        (self.etype, self.machine, _ver, self.entry, self.phoff, self.shoff,
         self.flags, _ehsize, _phentsize, self.phnum, shentsize, shnum,
         shstrndx) = struct.unpack_from('<HHIIIIIHHHHHH', data, 16)
        if self.machine != 243:
            raise ValueError(f'{name}: не RISC-V (machine={self.machine})')

        raw = []
        for i in range(shnum):
            o = self.shoff + i * shentsize
            (nm, typ, fl, addr, off, size, link, info, align,
             entsize) = struct.unpack_from('<IIIIIIIIII', data, o)
            raw.append(Section(i, str(nm), typ, fl, addr, off, size, link, info,
                               align, entsize))
        shstr = raw[shstrndx]
        strtab = data[shstr.offset:shstr.offset + shstr.size]
        for s in raw:
            s.name = cstr(strtab, int(s.name))
            s.data = b'' if s.type == SHT_NOBITS else data[s.offset:s.offset + s.size]
        self.sections = raw
        self.by_name = {s.name: s for s in raw}

        self.symbols: list[Symbol] = []
        self.symtab_idx = None
        for s in raw:
            if s.type != SHT_SYMTAB:
                continue
            self.symtab_idx = s.idx
            names = raw[s.link].data
            for i in range(s.size // 16):
                v, val, sz, info, other, shndx = struct.unpack_from('<IIIBBH', s.data, i * 16)
                self.symbols.append(Symbol(i, cstr(names, v), val, sz, info, other, shndx))
            break

    def relocs(self, sec: Section) -> list[Reloc]:
        """Релокации, относящиеся к данной секции."""
        out = []
        for r in self.sections:
            if r.type != SHT_RELA or r.info != sec.idx:
                continue
            for i in range(r.size // 12):
                off, info, add = struct.unpack_from('<IIi', r.data, i * 12)
                out.append(Reloc(off, info >> 8, info & 0xFF, add))
        return out

    def symbol(self, name):
        for s in self.symbols:
            if s.name == name and s.defined:
                return s
        return None

    def alloc_sections(self):
        return [s for s in self.sections if s.alloc and s.size]


def cstr(buf: bytes, off: int) -> str:
    end = buf.find(b'\0', off)
    return buf[off:end if end >= 0 else len(buf)].decode('utf-8', 'replace')


def section_bytes(elf: Elf32, name: str) -> bytes:
    s = elf.by_name.get(name)
    return s.data if s else b''
