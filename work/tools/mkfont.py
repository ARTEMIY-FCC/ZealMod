#!/usr/bin/env python3
"""TTF -> атлас глифов 4bpp + метрики, C-исходник build/fonts.c."""
import os, sys
from PIL import Image, ImageFont, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', 'build', 'fonts.c')
TTF_BOLD = '/System/Library/Fonts/Supplemental/Arial Bold.ttf'
TTF_REG = '/System/Library/Fonts/Supplemental/Arial.ttf'

LAT = ''.join(chr(c) for c in range(32, 127))
CYR = ''.join(chr(c) for c in range(0x410, 0x450)) + 'Ёё'
SYM = '◀▶▲▼•♪'
SYM_ALT = '◀▶'          # в Arial их нет — берём из Apple Symbols
TTF_SYM = '/System/Library/Fonts/Apple Symbols.ttf'
DIG = '0123456789:.,-+/%°'

FONTS = [
    ('font_s',  TTF_REG,  11, LAT + CYR + SYM),
    ('font_m',  TTF_BOLD, 15, LAT + CYR + SYM),
    ('font_l',  TTF_BOLD, 21, LAT + CYR + SYM),
    ('font_xl', TTF_BOLD, 46, DIG),
]


def render(name, ttf, size, chars):
    f = ImageFont.truetype(ttf, size)
    fsym = ImageFont.truetype(TTF_BOLD, size)   # стрелки есть только в жирном
    falt = ImageFont.truetype(TTF_SYM, size)
    asc, desc = f.getmetrics()
    glyphs = []
    bits = bytearray()
    for ch in sorted(set(chars), key=ord):
        # bbox глифа
        use = falt if ch in SYM_ALT else (fsym if ch in SYM else f)
        img = Image.new('L', (size * 3, size * 3), 0)
        d = ImageDraw.Draw(img)
        d.text((size, size), ch, font=use, fill=255, anchor='ls')  # перо на базовой линии
        bb = img.getbbox()
        adv = int(round(use.getlength(ch)))
        if bb is None:
            glyphs.append((ord(ch), 0, 0, 0, 0, adv, len(bits)))
            continue
        x0, y0, x1, y1 = bb
        w, h = x1 - x0, y1 - y0
        ox, oy = x0 - size, y0 - size          # от пера: x — влево, y — вверх от базовой
        off = len(bits)
        crop = img.crop(bb)
        rowb = (w + 1) // 2
        for y in range(h):
            row = bytearray(rowb)
            for x in range(w):
                a = crop.getpixel((x, y)) >> 4
                if x & 1: row[x >> 1] |= a
                else:     row[x >> 1] |= a << 4
            bits += row
        glyphs.append((ord(ch), w, h, ox, oy, adv, off))
    return glyphs, bits, asc, asc + desc


def emit(fh, name, glyphs, bits, base, line):
    def arr(t, sfx, vals, per=24):
        fh.write(f'static const {t} {name}_{sfx}[] = {{')
        for i, v in enumerate(vals):
            if i % per == 0: fh.write('\n    ')
            fh.write(f'{v},')
        fh.write('\n};\n')
    fh.write(f'/* {name}: {len(glyphs)} глифов, {len(bits)} байт */\n')
    fh.write(f'static const uint8_t {name}_bm[] = {{')
    for i, v in enumerate(bits):
        if i % 24 == 0: fh.write('\n    ')
        fh.write(f'{v},')
    fh.write('\n};\n')
    arr('uint8_t', 'w', [g[1] for g in glyphs])
    arr('uint8_t', 'h', [g[2] for g in glyphs])
    arr('int8_t', 'ox', [g[3] for g in glyphs])
    arr('int8_t', 'oy', [g[4] for g in glyphs])
    arr('uint8_t', 'adv', [g[5] for g in glyphs])
    arr('uint32_t', 'off', [g[6] for g in glyphs], 12)
    arr('uint16_t', 'cp', [g[0] for g in glyphs], 12)
    fh.write(f'const font_t {name} = {{ {name}_bm, {name}_w, {name}_h, {name}_ox, {name}_oy,'
             f' {name}_adv, {name}_off, {name}_cp, {len(glyphs)}, {line}, {base} }};\n\n')


def main():
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    total = 0
    with open(OUT, 'w') as fh:
        fh.write('/* сгенерировано tools/mkfont.py — не править */\n#include "plat.h"\n\n')
        for name, ttf, size, chars in FONTS:
            g, bits, base, line = render(name, ttf, size, chars)
            emit(fh, name, g, bits, base, line)
            total += len(bits)
            print(f'{name}: {len(g)} глифов, {len(bits)} Б, base={base} line={line}')
    print(f'итого битмапов: {total} Б -> {OUT}')


main()
