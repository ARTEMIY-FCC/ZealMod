"""Применение релокаций RISC-V (rv32imc, без relax).

Модуль из .zm приходит объектным файлом: адреса в нём ещё не расставлены.
Studio раскладывает секции по свободным адресам образа и правит инструкции
здесь.  Компилятор с ключами SDK (-mno-relax, без PIC) порождает всего пять
типов релокаций, остальные поддержаны на всякий случай.
"""
import struct

R_RISCV_NONE = 0
R_RISCV_32 = 1
R_RISCV_64 = 2
R_RISCV_BRANCH = 16
R_RISCV_JAL = 17
R_RISCV_CALL = 18
R_RISCV_CALL_PLT = 19
R_RISCV_GOT_HI20 = 20
R_RISCV_PCREL_HI20 = 23
R_RISCV_PCREL_LO12_I = 24
R_RISCV_PCREL_LO12_S = 25
R_RISCV_HI20 = 26
R_RISCV_LO12_I = 27
R_RISCV_LO12_S = 28
R_RISCV_ADD8, R_RISCV_ADD16, R_RISCV_ADD32, R_RISCV_ADD64 = 34, 35, 36, 37
R_RISCV_SUB8, R_RISCV_SUB16, R_RISCV_SUB32, R_RISCV_SUB64 = 38, 39, 40, 41
R_RISCV_ALIGN = 43
R_RISCV_RVC_BRANCH = 44
R_RISCV_RVC_JUMP = 45
R_RISCV_RELAX = 51
R_RISCV_SUB6, R_RISCV_SET6, R_RISCV_SET8, R_RISCV_SET16, R_RISCV_SET32 = 52, 53, 54, 55, 56
R_RISCV_32_PCREL = 57

NAMES = {v: k for k, v in list(globals().items()) if k.startswith('R_RISCV_')}

# релокации, которые ничего не пишут
IGNORED = {R_RISCV_NONE, R_RISCV_RELAX}


class RelocError(Exception):
    pass


def _s32(v):
    v &= 0xFFFFFFFF
    return v - (1 << 32) if v & 0x80000000 else v


def _u32(b, o):
    return struct.unpack_from('<I', b, o)[0]


def _p32(b, o, v):
    struct.pack_into('<I', b, o, v & 0xFFFFFFFF)


def _u16(b, o):
    return struct.unpack_from('<H', b, o)[0]


def _p16(b, o, v):
    struct.pack_into('<H', b, o, v & 0xFFFF)


def _hi20(v):
    """Старшие 20 бит с поправкой на знаковое расширение младших 12."""
    return ((v + 0x800) >> 12) & 0xFFFFF


def _lo12(v):
    return v & 0xFFF


def _put_u(buf, off, hi):
    _p32(buf, off, (_u32(buf, off) & 0x00000FFF) | (hi << 12))


def _put_i(buf, off, lo):
    _p32(buf, off, (_u32(buf, off) & 0x000FFFFF) | (lo << 20))


def _put_s(buf, off, lo):
    ins = _u32(buf, off) & ~0xFE000F80 & 0xFFFFFFFF
    _p32(buf, off, ins | ((lo >> 5) & 0x7F) << 25 | (lo & 0x1F) << 7)


def _put_b(buf, off, imm):
    if imm & 1 or not -(1 << 12) <= imm < (1 << 12):
        raise RelocError(f'branch вне досягаемости: {imm:#x}')
    ins = _u32(buf, off) & ~0xFE000F80 & 0xFFFFFFFF
    ins |= ((imm >> 12) & 1) << 31 | ((imm >> 5) & 0x3F) << 25
    ins |= ((imm >> 1) & 0xF) << 8 | ((imm >> 11) & 1) << 7
    _p32(buf, off, ins)


def _put_j(buf, off, imm):
    if imm & 1 or not -(1 << 20) <= imm < (1 << 20):
        raise RelocError(f'jal вне досягаемости: {imm:#x}')
    ins = _u32(buf, off) & 0x00000FFF
    ins |= ((imm >> 20) & 1) << 31 | ((imm >> 1) & 0x3FF) << 21
    ins |= ((imm >> 11) & 1) << 20 | ((imm >> 12) & 0xFF) << 12
    _p32(buf, off, ins)


def _put_cj(buf, off, imm):
    if imm & 1 or not -(1 << 11) <= imm < (1 << 11):
        raise RelocError(f'c.j вне досягаемости: {imm:#x}')
    ins = _u16(buf, off) & 0xE003
    for bit, pos in ((11, 12), (4, 11), (9, 10), (8, 9), (10, 8),
                     (6, 7), (7, 6), (3, 5), (2, 4), (1, 3), (5, 2)):
        ins |= ((imm >> bit) & 1) << pos
    _p16(buf, off, ins)


def _put_cb(buf, off, imm):
    if imm & 1 or not -(1 << 8) <= imm < (1 << 8):
        raise RelocError(f'c.branch вне досягаемости: {imm:#x}')
    ins = _u16(buf, off) & 0xE383
    for bit, pos in ((8, 12), (4, 11), (3, 10), (7, 6), (6, 5),
                     (2, 4), (1, 3), (5, 2)):
        ins |= ((imm >> bit) & 1) << pos
    _p16(buf, off, ins)


def apply(buf: bytearray, sec_addr: int, relocs, resolve, pcrel_hi=None, where=''):
    """Правит `buf` — содержимое одной секции, лежащей по адресу sec_addr.

    relocs   — список Reloc (elf.py)
    resolve  — функция (индекс символа) -> абсолютный адрес
    pcrel_hi — словарь {адрес инструкции auipc: значение S+A}, общий на объект:
               PCREL_LO12 ссылается на *метку* парного PCREL_HI20.
    """
    if pcrel_hi is None:
        pcrel_hi = {}
    # первый проход: запомнить, куда смотрит каждый auipc с PCREL_HI20
    for r in relocs:
        if r.type in (R_RISCV_PCREL_HI20, R_RISCV_GOT_HI20):
            pcrel_hi[sec_addr + r.where] = resolve(r.sym) + r.addend

    for r in relocs:
        if r.type in IGNORED:
            continue
        off = r.where
        p = sec_addr + off
        s = resolve(r.sym) + r.addend
        t = r.type
        try:
            if t == R_RISCV_32 or t == R_RISCV_SET32:
                _p32(buf, off, s)
            elif t == R_RISCV_64:
                struct.pack_into('<Q', buf, off, s & 0xFFFFFFFFFFFFFFFF)
            elif t == R_RISCV_32_PCREL:
                _p32(buf, off, s - p)
            elif t == R_RISCV_HI20:
                _put_u(buf, off, _hi20(s))
            elif t == R_RISCV_LO12_I:
                _put_i(buf, off, _lo12(s))
            elif t == R_RISCV_LO12_S:
                _put_s(buf, off, _lo12(s))
            elif t in (R_RISCV_CALL, R_RISCV_CALL_PLT):
                d = s - p
                if not -(1 << 31) <= d < (1 << 31):
                    raise RelocError('call слишком далеко')
                _put_u(buf, off, _hi20(d))
                _put_i(buf, off + 4, _lo12(d))
            elif t in (R_RISCV_PCREL_HI20, R_RISCV_GOT_HI20):
                if t == R_RISCV_GOT_HI20:
                    raise RelocError('нужен GOT — модуль собран как PIC, пересоберите SDK-ключами')
                _put_u(buf, off, _hi20(s - p))
            elif t in (R_RISCV_PCREL_LO12_I, R_RISCV_PCREL_LO12_S):
                base = pcrel_hi.get(s)
                if base is None:
                    raise RelocError(f'не нашёл парный auipc для {s:#x}')
                d = base - s
                (_put_i if t == R_RISCV_PCREL_LO12_I else _put_s)(buf, off, _lo12(d))
            elif t == R_RISCV_BRANCH:
                _put_b(buf, off, s - p)
            elif t == R_RISCV_JAL:
                _put_j(buf, off, s - p)
            elif t == R_RISCV_RVC_JUMP:
                _put_cj(buf, off, s - p)
            elif t == R_RISCV_RVC_BRANCH:
                _put_cb(buf, off, s - p)
            elif t in (R_RISCV_ADD8, R_RISCV_SUB8, R_RISCV_SET8):
                cur = buf[off]
                buf[off] = (cur + s if t == R_RISCV_ADD8 else
                            cur - s if t == R_RISCV_SUB8 else s) & 0xFF
            elif t in (R_RISCV_ADD16, R_RISCV_SUB16, R_RISCV_SET16):
                cur = _u16(buf, off)
                _p16(buf, off, cur + s if t == R_RISCV_ADD16 else
                     cur - s if t == R_RISCV_SUB16 else s)
            elif t in (R_RISCV_ADD32, R_RISCV_SUB32):
                cur = _u32(buf, off)
                _p32(buf, off, cur + s if t == R_RISCV_ADD32 else cur - s)
            elif t in (R_RISCV_SET6, R_RISCV_SUB6):
                cur = buf[off]
                v = (s & 0x3F) if t == R_RISCV_SET6 else ((cur - s) & 0x3F)
                buf[off] = (cur & 0xC0) | v
            elif t == R_RISCV_ALIGN:
                raise RelocError('R_RISCV_ALIGN: модуль собран с relax, нужен -mno-relax')
            else:
                raise RelocError(f'неизвестная релокация {NAMES.get(t, t)}')
        except RelocError as e:
            raise RelocError(f'{where}+{off:#x} ({NAMES.get(t, t)}): {e}') from None
