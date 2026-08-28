#!/usr/bin/env python3
"""Обложки для cover flow: 96x96, 8bpp + своя палитра на каждую. build/covers.c"""
import os, math
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', 'build', 'covers.c')
S = 96
F = '/System/Library/Fonts/Supplemental/Arial Bold.ttf'


def base(c1, c2):
    im = Image.new('RGB', (S, S))
    d = ImageDraw.Draw(im)
    for y in range(S):
        t = y / (S - 1)
        d.line([(0, y), (S, y)], fill=tuple(int(a + (b - a) * t) for a, b in zip(c1, c2)))
    return im, d


def cap(d, text, fill=(255, 255, 255), size=15, y=S - 21):
    f = ImageFont.truetype(F, size)
    w = d.textlength(text, font=f)
    d.text(((S - w) / 2, y), text, font=f, fill=fill)


def doom():
    im, d = base((90, 12, 8), (24, 6, 6))
    for i in range(9):                       # коридор
        k = i / 8.0
        w = int(80 * (1 - k) + 8)
        d.rectangle([48 - w // 2, 20 + int(k * 18), 48 + w // 2, 74 - int(k * 18)],
                    outline=(150 - i * 12, 40, 20))
    d.polygon([(30, 96), (48, 52), (66, 96)], fill=(180, 40, 20))   # ствол
    d.ellipse([36, 30, 60, 54], fill=(190, 160, 120))               # морда
    d.ellipse([41, 38, 46, 44], fill=(60, 20, 20)); d.ellipse([50, 38, 55, 44], fill=(60, 20, 20))
    d.rectangle([38, 47, 58, 50], fill=(120, 30, 30))
    return im


def pong():
    im, d = base((8, 8, 10), (0, 0, 0))
    for y in range(4, S, 12): d.rectangle([46, y, 49, y + 6], fill=(70, 70, 80))
    d.rectangle([8, 30, 14, 66], fill=(80, 220, 255))
    d.rectangle([82, 44, 88, 80], fill=(255, 120, 80))
    d.ellipse([52, 40, 64, 52], fill=(255, 255, 255))
    return im


def g2048():
    im, d = base((250, 248, 239), (222, 214, 200))
    cols = [(238, 228, 218), (237, 224, 200), (242, 177, 121), (246, 149, 99), (246, 124, 95)]
    n = 0
    for r in range(4):
        for c in range(4):
            x, y = 6 + c * 22, 6 + r * 22
            d.rounded_rectangle([x, y, x + 19, y + 19], 3, fill=cols[(r + c) % 5])
            n += 1
    f = ImageFont.truetype(F, 13)
    for (r, c, t) in ((1, 1, '2'), (1, 2, '4'), (2, 1, '8'), (2, 2, '16')):
        x, y = 6 + c * 22, 6 + r * 22
        w = d.textlength(t, font=f)
        d.text((x + (19 - w) / 2, y + 2), t, font=f, fill=(119, 110, 101))
    return im


def snake():
    im, d = base((30, 60, 24), (12, 28, 12))
    for i in range(0, S, 12): d.line([(i, 0), (i, S)], fill=(20, 44, 18)); d.line([(0, i), (S, i)], fill=(20, 44, 18))
    body = [(2, 5), (3, 5), (4, 5), (4, 4), (4, 3), (5, 3), (6, 3)]
    for i, (c, r) in enumerate(body):
        k = 160 + i * 12
        d.rounded_rectangle([c * 12 + 1, r * 12 + 1, c * 12 + 11, r * 12 + 11], 3, fill=(90, k, 70))
    d.ellipse([6 * 12 + 3, 3 * 12 + 3, 6 * 12 + 9, 3 * 12 + 9], fill=(240, 240, 240))
    d.ellipse([2 * 12 + 2, 1 * 12 + 2, 2 * 12 + 10, 1 * 12 + 10], fill=(220, 60, 50))
    return im


def tetris():
    im, d = base((16, 18, 40), (6, 6, 18))
    cols = {'I': (0, 240, 240), 'O': (240, 240, 0), 'T': (160, 0, 240), 'S': (0, 240, 0),
            'Z': (240, 0, 0), 'J': (0, 0, 240), 'L': (240, 160, 0)}
    cells = [(0, 7, 'J'), (1, 7, 'J'), (2, 7, 'J'), (2, 6, 'J'), (3, 7, 'O'), (4, 7, 'O'),
             (3, 6, 'O'), (4, 6, 'O'), (5, 7, 'S'), (6, 7, 'S'), (6, 6, 'S'), (7, 6, 'S'),
             (2, 3, 'T'), (3, 3, 'T'), (4, 3, 'T'), (3, 2, 'T'), (5, 0, 'I'), (5, 1, 'I'),
             (5, 2, 'I'), (5, 3, 'I')]
    for c, r, k in cells:
        x, y = c * 12, r * 12
        col = cols[k]
        d.rectangle([x, y, x + 11, y + 11], fill=col)
        d.rectangle([x, y, x + 11, y + 2], fill=tuple(min(255, v + 60) for v in col))
        d.rectangle([x, y + 9, x + 11, y + 11], fill=tuple(v // 2 for v in col))
    return im


def maze():
    im, d = base((20, 24, 60), (8, 10, 30))
    import random
    random.seed(7)
    for r in range(8):
        for c in range(8):
            if (r * 7 + c * 3) % 5 < 2:
                d.rectangle([c * 12, r * 12, c * 12 + 11, r * 12 + 11], fill=(70, 90, 200))
    d.ellipse([4, 4, 14, 14], fill=(255, 220, 60))
    d.rectangle([80, 80, 92, 92], fill=(80, 240, 120))
    return im


def maze3d():
    im, d = base((10, 10, 16), (4, 4, 8))
    for i in range(7):
        k = i / 6.0
        w = int(88 * (1 - k * 0.86))
        h = int(88 * (1 - k * 0.86))
        col = int(180 - 150 * k)
        d.rectangle([48 - w // 2, 48 - h // 2, 48 + w // 2, 48 + h // 2],
                    outline=(col, col // 2 + 40, 60), width=2)
    d.polygon([(4, 92), (48, 50), (92, 92)], fill=(40, 40, 60))
    return im


def mine():
    """Кубический мир: земляной блок на первом плане, холмы и дерево позади."""
    im, d = base((116, 176, 238), (196, 220, 240))
    d.rectangle([0, 58, S, S], fill=(120, 168, 96))          # дальний план
    # холм из кубиков
    for r, (x0, y0, w) in enumerate(((6, 50, 5), (18, 44, 3), (30, 38, 2))):
        for i in range(w):
            x, y = x0 + i * 10, y0
            d.polygon([(x, y), (x + 5, y - 3), (x + 15, y - 3), (x + 10, y)], fill=(126, 188, 84))
            d.rectangle([x, y, x + 10, y + 10], fill=(104, 76, 52))
    # дерево
    d.rectangle([70, 34, 78, 60], fill=(88, 66, 42))
    for (x, y) in ((60, 16), (70, 12), (80, 16), (64, 26), (76, 26), (70, 22)):
        d.rectangle([x, y, x + 14, y + 14], fill=(44, 112, 46))
        d.rectangle([x, y, x + 14, y + 3], fill=(60, 138, 58))
    # крупный блок травы спереди, в изометрии
    tx, ty, w, h = 26, 60, 44, 26
    d.polygon([(tx, ty), (tx + w // 2, ty - 14), (tx + w, ty), (tx + w // 2, ty + 14)],
              fill=(112, 186, 74))                            # верх
    d.polygon([(tx, ty), (tx + w // 2, ty + 14), (tx + w // 2, ty + 14 + h), (tx, ty + h)],
              fill=(116, 84, 56))                             # левый бок
    d.polygon([(tx + w, ty), (tx + w // 2, ty + 14), (tx + w // 2, ty + 14 + h), (tx + w, ty + h)],
              fill=(92, 66, 44))                              # правый бок
    d.polygon([(tx, ty), (tx + w // 2, ty + 14), (tx + w // 2, ty + 19), (tx, ty + 5)],
              fill=(80, 150, 54))                             # зелёная кромка
    d.polygon([(tx + w, ty), (tx + w // 2, ty + 14), (tx + w // 2, ty + 19), (tx + w, ty + 5)],
              fill=(66, 126, 46))
    for (px_, py_) in ((32, 70), (40, 78), (52, 74), (58, 66), (36, 62)):
        d.rectangle([px_, py_, px_ + 3, py_ + 3], fill=(96, 70, 46))
    return im


def gravity():
    im, d = base((110, 180, 255), (220, 240, 255))
    pts = [(x, 70 + int(14 * math.sin(x / 15.0) + 6 * math.sin(x / 5.0))) for x in range(S + 1)]
    d.polygon(pts + [(S, S), (0, S)], fill=(70, 130, 50))
    d.line(pts, fill=(40, 90, 30), width=3)
    d.ellipse([28, 52, 44, 68], outline=(20, 20, 20), width=3)
    d.ellipse([56, 46, 72, 62], outline=(20, 20, 20), width=3)
    d.line([(36, 60), (52, 48), (64, 54)], fill=(200, 30, 30), width=4)
    d.line([(48, 50), (46, 40)], fill=(20, 20, 20), width=3)
    d.ellipse([40, 30, 52, 42], fill=(230, 90, 40))
    return im


def clock():
    im, d = base((25, 28, 38), (8, 9, 14))
    d.ellipse([6, 6, 90, 90], outline=(200, 210, 230), width=3)
    for i in range(12):
        a = i * math.pi / 6
        r1, r2 = 34, 40
        d.line([(48 + r1 * math.sin(a), 48 - r1 * math.cos(a)),
                (48 + r2 * math.sin(a), 48 - r2 * math.cos(a))], fill=(150, 165, 190), width=2)
    d.line([(48, 48), (48 + 20 * math.sin(1.1), 48 - 20 * math.cos(1.1))], fill=(255, 255, 255), width=4)
    d.line([(48, 48), (48 + 30 * math.sin(3.4), 48 - 30 * math.cos(3.4))], fill=(255, 255, 255), width=3)
    d.line([(48, 48), (48 + 33 * math.sin(5.2), 48 - 33 * math.cos(5.2))], fill=(255, 80, 60), width=2)
    d.ellipse([45, 45, 51, 51], fill=(255, 80, 60))
    return im


def calendar():
    im, d = base((245, 245, 250), (210, 214, 226))
    d.rounded_rectangle([8, 12, 88, 88], 6, fill=(255, 255, 255), outline=(180, 185, 200))
    d.rounded_rectangle([8, 12, 88, 30], 6, fill=(210, 60, 60))
    d.rectangle([8, 26, 88, 30], fill=(210, 60, 60))
    for x in (26, 48, 70): d.rectangle([x - 3, 6, x + 3, 18], fill=(120, 125, 140))
    f = ImageFont.truetype(F, 30)
    t = '27'
    w = d.textlength(t, font=f)
    d.text(((S - w) / 2, 40), t, font=f, fill=(60, 62, 72))
    return im


def diag():
    im, d = base((40, 44, 58), (18, 20, 28))
    d.ellipse([18, 18, 78, 78], outline=(200, 210, 235), width=6)
    for i in range(8):
        a = i * math.pi / 4
        x, y = 48 + 36 * math.cos(a), 48 + 36 * math.sin(a)
        d.rectangle([x - 7, y - 7, x + 7, y + 7], fill=(200, 210, 235))
    d.ellipse([34, 34, 62, 62], fill=(18, 20, 28))
    d.line([(20, 84), (76, 84)], fill=(120, 200, 255), width=4)
    return im


def sound():
    im, d = base((28, 34, 52), (10, 12, 20))
    d.arc([20, 20, 76, 76], 200, 340, fill=(230, 235, 250), width=5)
    d.rounded_rectangle([18, 46, 32, 76], 6, fill=(230, 235, 250))
    d.rounded_rectangle([64, 46, 78, 76], 6, fill=(230, 235, 250))
    for i, r in enumerate((10, 18, 26)):
        d.arc([48 - r, 34 - r, 48 + r, 34 + r], 300, 60, fill=(120, 200, 255), width=3)
    return im


COVERS = [('sound', sound), ('diag', diag), ('doom', doom), ('mine', mine), ('pong', pong), ('g2048', g2048), ('snake', snake), ('tetris', tetris),
          ('maze', maze), ('maze3d', maze3d), ('gravity', gravity), ('clock', clock),
          ('calendar', calendar)]


def emit(fh, name, im):
    q = im.convert('P', palette=Image.ADAPTIVE, colors=256)
    pal = (q.getpalette() + [0] * 768)[:768]
    idx = list(q.getdata())
    fh.write(f'const uint8_t cover_{name}[{S*S}] = {{')
    for i, v in enumerate(idx):
        if i % 24 == 0: fh.write('\n    ')
        fh.write(f'{v},')
    fh.write('\n};\n')
    fh.write(f'const px cover_{name}_pal[256] = {{')
    for i in range(256):
        r, g, b = pal[i * 3:i * 3 + 3]
        if i % 8 == 0: fh.write('\n    ')
        fh.write(f'RGB({r},{g},{b}),')
    fh.write('\n};\n\n')


def main():
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, 'w') as fh:
        fh.write('/* сгенерировано tools/mkcovers.py */\n#include "plat.h"\n\n')
        for name, fn in COVERS:
            im = fn()
            emit(fh, name, im)
            im.resize((S * 3, S * 3), Image.NEAREST).save(
                os.path.join(HERE, '..', 'assets', 'covers', f'{name}.png'))
    print(f'{len(COVERS)} обложек -> {OUT}')


main()
