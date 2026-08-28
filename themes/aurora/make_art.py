#!/usr/bin/env python3
"""Aurora — обои и логотип темы: они считаются формулой, а не рисуются мышью.

    python3 themes/aurora/make_art.py

Сцена подогнана под геометрию меню (work/src/menu.c).  Глаз там на строке 100,
фокус 300, полуширина обложки 48 — значит основание обложки на расстоянии z
приходится на строку  y = 100 + 14400/z.  Это в точности перспектива
горизонтальной плоскости на высоте глаза: горизонт на y=100, всё ниже — вода.
Поэтому «отражение», которое ядро рисует само (зеркало вниз от основания),
садится ровно в отражение неба на обоях, а гребни волн расходятся по тем же
1/z, что и обложки.

Экран 16-битный: в конце картинка раскладывается по сетке RGB565 с диффузией
ошибки — иначе градиенты неба идут ступенями.
"""
import numpy as np
from PIL import Image, ImageDraw
from scipy.ndimage import gaussian_filter, map_coordinates
from pathlib import Path

W = H = 240
HORIZON = 100                      # уровень глаз из menu.c
TOP = -140                         # докуда нужно небо, чтобы хватило отражению
SKY_H = HORIZON - TOP + 1          # 241 строка: y = -140..100

X = np.arange(W, dtype=float)
SY = np.arange(TOP, HORIZON + 1, dtype=float)
xx, yy = np.meshgrid(X, SY)        # (241, 240)


# --- мелкая арифметика -----------------------------------------------------
def ramp(t, stops):
    """Плавный переход по опорным цветам."""
    t = np.clip(t, 0.0, 1.0)
    ts = [s[0] for s in stops]
    out = np.empty(t.shape + (3,))
    for c in range(3):
        out[..., c] = np.interp(t, ts, [s[1][c] for s in stops])
    return out


def wobble(x, seed, oct_=4, base=0.008):
    """Сумма синусов со случайными фазами: ровный «шум» без библиотек."""
    rng = np.random.default_rng(seed)
    v = np.zeros_like(x)
    amp, f = 1.0, base
    for _ in range(oct_):
        v += amp * np.sin(x * f * 2 * np.pi + rng.uniform(0, 2 * np.pi))
        amp *= 0.55
        f *= 2.0
    return v / 1.9


# --- небо ------------------------------------------------------------------
def sky_field():
    t = (yy - TOP) / (HORIZON - TOP)
    sky = ramp(t, [(0.00, (0.005, 0.007, 0.024)),
                   (0.42, (0.009, 0.018, 0.048)),
                   (0.64, (0.014, 0.034, 0.072)),
                   (0.86, (0.024, 0.064, 0.104)),
                   (1.00, (0.040, 0.120, 0.150))])

    # звёзды: гуще к зениту, у горизонта их съедает свечение
    rng = np.random.default_rng(20260828)
    buf = np.zeros((SKY_H, W))
    n = 520
    sx = rng.uniform(0, W - 1, n)
    sy = rng.uniform(TOP, HORIZON - 6, n)
    br = rng.random(n) ** 3.0 * 0.9 + 0.04
    keep = rng.random(n) < np.clip(0.10 + 0.90 * (HORIZON - sy) / 150.0, 0, 1)
    sx, sy, br = sx[keep], sy[keep], br[keep]
    for x0, y0, b in zip(sx, sy, br):
        r, c = y0 - TOP, x0
        i, j = int(r), int(c)
        fr, fc = r - i, c - j
        if 0 <= i < SKY_H - 1 and 0 <= j < W - 1:
            buf[i, j] += b * (1 - fr) * (1 - fc)
            buf[i, j + 1] += b * (1 - fr) * fc
            buf[i + 1, j] += b * fr * (1 - fc)
            buf[i + 1, j + 1] += b * fr * fc
    # несколько ярких — с крестиком
    for x0, y0, b in sorted(zip(sx, sy, br), key=lambda s: -s[2])[:9]:
        i, j = int(y0 - TOP), int(x0)
        for d in (1, 2, 3):
            f = b * (0.30 / d)
            for di, dj in ((d, 0), (-d, 0), (0, d), (0, -d)):
                if 0 <= i + di < SKY_H and 0 <= j + dj < W:
                    buf[i + di, j + dj] += f
    buf = gaussian_filter(buf, 0.5)
    sky += buf[..., None] * np.array([0.85, 0.92, 1.00])

    # сияние: занавесы. Низкий край резкий, вверх — гаснет лучами.
    def curtain(y0, amp, height, xc, xs, gain, colors, seed):
        base = y0 + amp * wobble(X, seed, 3, 0.004) * 2.0
        hgt = np.maximum(height * (0.72 + 0.45 * wobble(X, seed + 1, 3, 0.006)), 6.0)
        d = base[None, :] - yy
        up = np.exp(-np.clip(d, 0, None) / hgt[None, :])
        low = np.exp(-np.clip(-d, 0, None) ** 2 / 14.0)
        # лучи: занавес состоит из вертикальных нитей разной яркости
        rays = (0.30 + 0.70 * (0.5 + 0.5 * wobble(X, seed + 2, 6, 0.055))) ** 1.35
        win = np.exp(-((X - xc) / xs) ** 2) * (0.55 + 0.45 * (0.5 + 0.5 * wobble(X, seed + 3, 2, 0.010)))
        a = up * low * rays[None, :] * win[None, :] * gain
        col = ramp(np.clip(d / (hgt[None, :] * 1.5), 0, 1), colors)
        out = a[..., None] * col
        # яркая кромка по нижнему краю занавеса
        rim = np.exp(-(d / 2.6) ** 2) * low * rays[None, :] * win[None, :] * gain
        out += rim[..., None] * np.array(colors[0][1]) * 0.55
        return gaussian_filter(out, (0.9, 0.7, 0))

    green = (0.28, 1.00, 0.62)
    cyan = (0.26, 0.86, 0.96)
    violet = (0.52, 0.34, 0.92)
    sky += curtain(74, 6, 30, 44, 58, 1.05,
                   [(0.0, green), (0.45, cyan), (1.0, violet)], 11)
    sky += curtain(58, 8, 24, 96, 34, 0.45,
                   [(0.0, green), (0.50, cyan), (1.0, violet)], 23)
    sky += curtain(46, 9, 26, 200, 42, 0.62,
                   [(0.0, cyan), (0.40, (0.36, 0.62, 0.98)), (1.0, violet)], 31)
    # высоко — уже не занавес, а ровная фиолетовая дымка
    haze = (np.exp(-((yy - 30) / 36.0) ** 2)
            * (0.35 + 0.65 * np.exp(-((X - 150) / 105.0) ** 2))[None, :])
    sky += haze[..., None] * np.array([0.026, 0.024, 0.062])

    # засветка у горизонта — сильнее там, где стоят занавесы
    glowx = (0.40 + 0.60 * np.exp(-((X - 50) / 72.0) ** 2)
             + 0.40 * np.exp(-((X - 198) / 58.0) ** 2))
    glow = np.exp(-((yy - HORIZON) / 17.0) ** 2)
    sky += (glow * glowx[None, :])[..., None] * np.array([0.045, 0.190, 0.230])

    # дальний берег: тонкая тёмная кромка под самым горизонтом
    ridge = (HORIZON - 2.0 - 3.4 * (0.5 + 0.5 * wobble(X, 97, 4, 0.005))
             - 2.2 * np.clip(wobble(X, 71, 2, 0.018), 0, None))
    m = np.clip((yy - ridge[None, :]) * 1.6, 0, 1) * (yy <= HORIZON)
    sky = sky * (1 - 0.86 * m[..., None]) + m[..., None] * np.array([0.010, 0.020, 0.032])
    return sky


# --- вода ------------------------------------------------------------------
def lake(sky):
    ly = np.arange(HORIZON, H, dtype=float)                 # строки 100..239
    lx, lyy = np.meshgrid(X, ly)
    w = ((lyy - HORIZON) / (H - HORIZON))                   # 0 у горизонта, 1 внизу

    # зеркало относительно линии глаз: строка неба 200-y, в индексах массива
    src = (340.0 - lyy)
    dx = (0.5 + 7.5 * w ** 2) * (0.62 * np.sin(lx * 0.130 + w * 9.0)
                                 + 0.38 * np.sin(lx * 0.041 - w * 15.0))
    dy = (0.8 + 3.2 * w) * 0.35 * np.sin(lx * 0.085 + w * 6.0)

    soft = gaussian_filter(sky, (1.1, 0.5, 0))
    smear = gaussian_filter(sky, (5.0, 1.2, 0))
    out = np.empty((len(ly), W, 3))
    k = (w ** 0.7)[..., None]
    for c in range(3):
        a = map_coordinates(soft[..., c], [src + dy, lx + dx], order=1, mode='nearest')
        b = map_coordinates(smear[..., c], [src + dy, lx + dx], order=1, mode='nearest')
        out[..., c] = a * (1 - k[..., 0]) + b * k[..., 0]

    out *= np.array([0.60, 0.70, 0.80])                     # вода холоднее неба
    out *= (1.0 - 0.64 * w ** 1.30)[..., None]              # и темнее к ногам

    # гребни: те же 1/z, по каким расставлены обложки
    for z in (2600, 1700, 1150, 800, 570, 420, 320, 250, 200, 165, 138, 118, 102):
        yc = 14400.0 / z
        if yc >= H - HORIZON - 1:
            continue
        thick = 0.55 + 0.030 * yc
        prof = np.exp(-(((lyy - HORIZON) - yc) / thick) ** 2)
        mod = 0.35 + 0.65 * (0.5 + 0.5 * wobble(X, int(z), 4, 0.020))
        amp = 0.085 * np.exp(-yc / 120.0)
        out += (prof * mod[None, :] * amp)[..., None] * np.array([0.35, 0.95, 1.00])
    return out


def compose():
    sky = sky_field()
    img = np.empty((H, W, 3))
    img[:HORIZON] = sky[-TOP:-TOP + HORIZON]        # строки неба y = 0..99
    img[HORIZON:] = lake(sky)

    # сама кромка воды
    glowx = (0.40 + 0.60 * np.exp(-((X - 50) / 72.0) ** 2)
             + 0.40 * np.exp(-((X - 198) / 58.0) ** 2))
    line = (0.5 + 0.5 * glowx)[:, None] * np.array([0.10, 0.44, 0.52])
    img[HORIZON] += line * 0.95
    img[HORIZON - 1] += line * 0.30
    img[HORIZON + 1] += line * 0.45

    # виньетка и лёгкая тень в центре, чтобы обложки отделялись от фона
    gx, gy = np.meshgrid(X, np.arange(H, dtype=float))
    r2 = ((gx - 120) / 152.0) ** 2 + ((gy - 118) / 152.0) ** 2
    img *= (1.0 - 0.34 * r2)[..., None]
    img *= (1.0 - 0.11 * np.exp(-(((gx - 120) / 80.0) ** 2
                                  + ((gy - 98) / 60.0) ** 2)))[..., None]
    return np.clip(img, 0, 1)


# --- в 16 бит, с диффузией ошибки -----------------------------------------
def to565(img):
    """Раскладка по сетке экрана: ошибка уезжает соседям (Флойд-Стейнберг)."""
    a = np.clip(img, 0, 1) * 255.0
    lv = (31.0, 63.0, 31.0)
    rng = np.random.default_rng(4)
    jit = rng.uniform(-0.30, 0.30, (H, W, 3))
    out = np.zeros((H, W, 3), np.uint8)
    for y in range(H):
        for x in range(W):
            for c in range(3):
                v = a[y, x, c]
                idx = int(np.clip(round(v / 255.0 * lv[c] + jit[y, x, c]), 0, lv[c]))
                q = (idx << 3 | idx >> 2) if lv[c] == 31 else (idx << 2 | idx >> 4)
                out[y, x, c] = q
                e = (v - q) * 0.78
                if x + 1 < W:
                    a[y, x + 1, c] += e * 7 / 16
                if y + 1 < H:
                    if x:
                        a[y + 1, x - 1, c] += e * 3 / 16
                    a[y + 1, x, c] += e * 5 / 16
                    if x + 1 < W:
                        a[y + 1, x + 1, c] += e * 1 / 16
    return Image.fromarray(out, 'RGB')


# --- логотип для заставки --------------------------------------------------
def splash_bg():
    """Цвет, которым splash.c заливает экран: px_mix(bg_bot, BLACK, 128).

    Считается из самой темы и с тем же округлением до RGB565, что на часах, —
    иначе логотип виден на заставке отдельным квадратом."""
    import json
    c = json.loads((Path(__file__).resolve().parent / 'theme.json').read_text())
    h = c['colors']['bg_bot'].lstrip('#')
    r, g, b = (int(h[i:i + 2], 16) for i in (0, 2, 4))
    r, g, b = r & 0xF8, g & 0xFC, b & 0xF8
    r, g, b = r - (r * 128 >> 8), g - (g * 128 >> 8), b - (b * 128 >> 8)
    return np.array([r & 0xF8, g & 0xFC, b & 0xF8]) / 255.0


def logo():
    """128x128.  Фон совпадает с фоном заставки, знак — в цветах сияния."""
    S, K = 128, 4                                   # K — сглаживание рисованием крупнее
    bg = splash_bg()
    x = np.arange(S, dtype=float)
    gx, gy = np.meshgrid(x, x)
    img = np.tile(bg, (S, S, 1)).astype(float)

    # сияние за знаком
    halo = np.exp(-(((gx - 64) / 44.0) ** 2 + ((gy - 58) / 40.0) ** 2))
    img += halo[..., None] * np.array([0.05, 0.30, 0.26]) * 0.55

    # плитка со скруглением: считаем расстояние до её края
    hw = hh = 46.0
    r = 22.0
    qx = np.abs(gx - 63.5) - (hw - r)
    qy = np.abs(gy - 63.5) - (hh - r)
    sdf = np.hypot(np.maximum(qx, 0), np.maximum(qy, 0)) + np.minimum(np.maximum(qx, qy), 0) - r
    tile = np.clip(0.5 - sdf, 0, 1)
    face = ramp((gy / S), [(0.0, (0.043, 0.098, 0.137)), (1.0, (0.019, 0.043, 0.066))])
    sheen = np.clip(1 - np.abs((gx + gy * 0.75) - 96) / 46.0, 0, 1) ** 2 * 0.055
    img = img * (1 - tile[..., None]) + (face + sheen[..., None]) * tile[..., None]

    # внешнее свечение и кант плитки в цветах сияния
    edge = np.exp(-(np.clip(sdf, 0, None) / 7.0) ** 1.4)
    aur = ramp(np.clip((gy - 18) / 92.0, 0, 1),
               [(0.0, (0.40, 0.34, 0.92)), (0.5, (0.24, 0.82, 0.94)), (1.0, (0.26, 0.98, 0.66))])
    img += (edge * (1 - tile))[..., None] * aur * 0.42
    rim = np.exp(-(np.abs(sdf) / 1.5) ** 2)
    img += rim[..., None] * aur * 0.85

    # сам знак: рисуем крупно и уменьшаем — края получаются мягкими
    big = Image.new('L', (S * K, S * K), 0)
    d = ImageDraw.Draw(big)
    w, t = 54.0, 12.5
    x0, x1 = 64 - w / 2, 64 + w / 2
    y0, y1 = 64 - w / 2 + 1, 64 + w / 2 + 1
    sc = lambda p: [(px * K, py * K) for px, py in p]
    d.polygon(sc([(x0, y0), (x1, y0), (x1, y0 + t), (x0, y0 + t)]), fill=255)
    d.polygon(sc([(x0, y1 - t), (x1, y1 - t), (x1, y1), (x0, y1)]), fill=255)
    d.polygon(sc([(x1, y0 + t), (x1 - t * 1.30, y0 + t),
                  (x0, y1 - t), (x0 + t * 1.30, y1 - t)]), fill=255)
    mark = np.asarray(big.resize((S, S), Image.LANCZOS), float) / 255.0
    mark = np.clip(mark, 0, 1)

    img += (gaussian_filter(mark, 4.0) * 0.55 + gaussian_filter(mark, 11.0) * 0.45)[..., None] \
        * np.array([0.16, 0.85, 0.68])
    ink = ramp(np.clip((gy - 34) / 60.0, 0, 1),
               [(0.0, (0.90, 1.00, 0.97)), (1.0, (0.36, 0.94, 0.80))])
    img = img * (1 - mark[..., None]) + ink * mark[..., None]

    # За кругом r=70 остаётся ровно цвет фона заставки.  Иначе там гуляют
    # значения на единицу, палитра из 256 цветов их разводит дизерингом, и
    # квадрат картинки становится виден на ровной заливке вокруг.
    m = np.clip((70.0 - np.hypot(gx - 63.5, gy - 63.5)) / 12.0, 0, 1)[..., None]
    img = bg + (img - bg) * m
    return Image.fromarray((np.clip(img, 0, 1) * 255 + 0.5).astype(np.uint8), 'RGB')


if __name__ == '__main__':
    here = Path(__file__).resolve().parent
    to565(compose()).save(here / 'wallpaper.png')
    logo().save(here / 'logo.png')
    print('wallpaper.png, logo.png ->', here)
