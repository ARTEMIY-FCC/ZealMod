#!/usr/bin/env python3
"""WAD -> данные для движка на таймере.

Из WAD берутся только опубликованные форматы файлов: карты идут со своими
BSP-деревьями, текстуры собираются из патчей.  Ничего из кода id Software не
используется, а сами ресурсы — Freedoom.

  python3 tools/mkdoom.py --wad ~/Desktop/.zepgam-wad/freedoom-0.13.0/freedoom1.wad \
                          --maps E1M1 E1M2 E1M3
Пишет build/doom_gfx.bin и build/doom_maps.bin, плюс build/doom_data.S с .incbin.
"""
import argparse, os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wad as W

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', 'build')
TEXBIG = 64          # столько дают самым ходовым текстурам
TEXSMALL = 32        # остальным
FLATN = 32           # флэты 64x64 -> 32x32

# ---------------------------------------------------------------------------


def rgb565_be(r, g, b):
    v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
    return ((v >> 8) & 0xFF) | ((v & 0xFF) << 8)


class Blob:
    def __init__(self):
        self.b = bytearray()

    def align(self, n=4):
        while len(self.b) % n:
            self.b.append(0)
        return len(self.b)

    def add(self, data, align=4):
        self.align(align)
        o = len(self.b)
        self.b += data
        return o


def scale_indices(px, w, h, nw, nh, pal):
    """Уменьшение палитровой картинки усреднением в RGB и поиском ближайшего."""
    out = bytearray(nw * nh)
    for y in range(nh):
        y0, y1 = y * h // nh, max(y * h // nh + 1, (y + 1) * h // nh)
        for x in range(nw):
            x0, x1 = x * w // nw, max(x * w // nw + 1, (x + 1) * w // nw)
            r = g = b = n = 0
            for yy in range(y0, y1):
                row = yy * w
                for xx in range(x0, x1):
                    c = px[row + xx]
                    if c is None:
                        continue
                    cr, cg, cb = pal[c]
                    r += cr; g += cg; b += cb; n += 1
            out[y * nw + x] = 255 if not n else nearest(pal, r // n, g // n, b // n)
    return out


_near = {}


def nearest(pal, r, g, b):
    k = (r >> 2, g >> 2, b >> 2)
    v = _near.get(k)
    if v is not None:
        return v
    best, bd = 0, 1 << 30
    for i in range(255):            # 255 оставляем под «дырку»
        pr, pg, pb = pal[i]
        d = (r - pr) ** 2 + (g - pg) ** 2 + (b - pb) ** 2
        if d < bd:
            bd, best = d, i
    _near[k] = best
    return best


def pow2(n):
    p = 1
    while p < n:
        p *= 2
    return p


# тип вещи -> (имя спрайта, кадры, поворотов, радиус, высота, здоровье, флаги
#              [, кадры смерти])
# флаги: 1 подбирается, 2 сплошная, 4 стреляет в ответ, 8 висит
# кадры смерти идут без поворотов; 'BEXP:ABCDE' — если они у другого спрайта
THINGS = {
    2001: ('SHOT', 'A', 1, 20, 16, 0, 1), 2002: ('MGUN', 'A', 1, 20, 16, 0, 1),
    2005: ('CSAW', 'A', 1, 20, 16, 0, 1), 2003: ('LAUN', 'A', 1, 20, 16, 0, 1),
    2007: ('CLIP', 'A', 1, 20, 16, 0, 1), 2008: ('SHEL', 'A', 1, 20, 16, 0, 1),
    2048: ('AMMO', 'A', 1, 20, 16, 0, 1), 2049: ('SBOX', 'A', 1, 20, 16, 0, 1),
    2010: ('ROCK', 'A', 1, 20, 16, 0, 1), 2046: ('BROK', 'A', 1, 20, 16, 0, 1),
    2011: ('STIM', 'A', 1, 20, 16, 0, 1), 2012: ('MEDI', 'A', 1, 20, 16, 0, 1),
    2014: ('BON1', 'ABCD', 1, 20, 16, 0, 1), 2015: ('BON2', 'ABCD', 1, 20, 16, 0, 1),
    2018: ('ARM1', 'AB', 1, 20, 16, 0, 1), 2019: ('ARM2', 'AB', 1, 20, 16, 0, 1),
    2013: ('SOUL', 'ABCDCB', 1, 20, 16, 0, 1), 8: ('BPAK', 'A', 1, 20, 16, 0, 1),
    2026: ('PMAP', 'ABCD', 1, 20, 16, 0, 1), 2024: ('PINS', 'ABCD', 1, 20, 16, 0, 1),
    5: ('BKEY', 'AB', 1, 20, 16, 0, 1), 6: ('YKEY', 'AB', 1, 20, 16, 0, 1),
    13: ('RKEY', 'AB', 1, 20, 16, 0, 1), 40: ('BSKU', 'AB', 1, 20, 16, 0, 1),
    38: ('RSKU', 'AB', 1, 20, 16, 0, 1), 39: ('YSKU', 'AB', 1, 20, 16, 0, 1),
    2035: ('BAR1', 'AB', 1, 10, 42, 20, 2, 'BEXP:ABCDE'),
    2028: ('COLU', 'A', 1, 16, 48, 0, 2), 30: ('COL1', 'A', 1, 16, 52, 0, 2),
    31: ('COL2', 'A', 1, 16, 52, 0, 2), 32: ('COL3', 'A', 1, 16, 52, 0, 2),
    33: ('COL4', 'A', 1, 16, 52, 0, 2), 47: ('SMIT', 'A', 1, 16, 40, 0, 2),
    48: ('ELEC', 'A', 1, 16, 60, 0, 2), 43: ('TRE1', 'A', 1, 16, 56, 0, 2),
    54: ('TRE2', 'A', 1, 32, 100, 0, 2), 35: ('CBRA', 'A', 1, 16, 60, 0, 2),
    3004: ('POSS', 'ABCD', 8, 20, 56, 20, 4, 'HIJKL'),
    9:    ('SPOS', 'ABCD', 8, 20, 56, 30, 4, 'HIJKL'),
    3001: ('TROO', 'ABCD', 8, 20, 56, 60, 4, 'IJKLM'),
    3002: ('SARG', 'ABCD', 8, 30, 56, 150, 4, 'IJKLMN'),
    58:   ('SARG', 'ABCD', 8, 30, 56, 150, 4, 'IJKLMN'),
}
SPRITE_SCALE = 2          # спрайты вдвое мельче: экран всё равно 240 точек


def sprite_lumps(w):
    """имя -> {кадр: {поворот: (лумп, зеркалить)}}"""
    out = {}
    for nm, li in w.between('S_START', 'S_END'):
        if len(nm) < 6:
            continue
        spr, fr, rot = nm[0:4], nm[4], nm[5]
        if not ('A' <= fr <= 'Z') or not rot.isdigit():
            continue
        out.setdefault(spr, {}).setdefault(fr, {})[int(rot)] = (nm, False)
        if len(nm) >= 8 and 'A' <= nm[6] <= 'Z' and nm[7].isdigit():
            out.setdefault(spr, {}).setdefault(nm[6], {})[int(nm[7])] = (nm, True)
    return out


def add_frame(w, pal, tab, data, seen, pick):
    """Один кадр спрайта: уменьшить, разложить по столбцам, положить в блоб."""
    if not pick:
        tab.append((1, 1, 0, 0, 0)); return
    lname, mirror = pick
    lump = w.get(lname)
    try:
        pw, ph, xo, yo, px = W.patch_to_indices(lump)
    except Exception:
        tab.append((1, 1, 0, 0, 0)); return
    if mirror:
        px = [px[y * pw + (pw - 1 - x)] for y in range(ph) for x in range(pw)]
        xo = pw - xo
    nw, nh = max(1, pw // SPRITE_SCALE), max(1, ph // SPRITE_SCALE)
    small = scale_indices(px, pw, ph, nw, nh, pal)
    col = bytearray(nw * nh)                          # по столбцам
    for x in range(nw):
        for y in range(nh):
            col[x * nh + y] = small[y * nw + x]
    key = bytes(col)
    off = seen.get(key)
    if off is None:
        off = data.add(key, 1); seen[key] = off
    tab.append((nw, nh, xo // SPRITE_SCALE, yo // SPRITE_SCALE, off))


def build_sprites(w, pal, maps_things, gfx):
    """Кладёт в блоб все спрайты, нужные вещам этих карт."""
    lumps = sprite_lumps(w)
    used = set()
    for t in maps_things:
        if t in THINGS:
            used.add(t)
    order = sorted(used)
    tab, data, seen = [], Blob(), {}
    info = []
    for ty in order:
        ent = THINGS[ty]
        name, frames, nrots, radius, height, hp, flags = ent[:7]
        death = ent[7] if len(ent) > 7 else ''
        base = len(tab)
        fr_map = lumps.get(name, {})
        for fr in frames:
            rots = fr_map.get(fr, {})
            for r in range(nrots):
                pick = rots.get(r + 1 if nrots > 1 else 0) or rots.get(0) or rots.get(1)
                add_frame(w, pal, tab, data, seen, pick)
        dbase, ndeath = base, 0
        if death:
            dname, _, dfr = death.rpartition(':')
            dmap = lumps.get(dname or name, {})
            dbase = len(tab)
            for fr in dfr:
                add_frame(w, pal, tab, data, seen, dmap.get(fr, {}).get(0))
                ndeath += 1
        info.append((ty, base, len(frames), nrots, flags, hp, radius, height,
                     dbase, ndeath, 0))
    tab_off = gfx.add(b''.join(struct.pack('<HHhhI', *t) for t in tab))
    data_off = gfx.add(bytes(data.b))
    info_off = gfx.add(b''.join(struct.pack('<HHBBBBhhHBB', *i) for i in info))
    print(f'  спрайтов: {len(tab)} кадров, {len(data.b)} Б; типов вещей {len(info)}')
    return tab_off, data_off, info_off, len(info)


# оружие в руках рисуется как есть, без уменьшения
WEAPONS = ('PISGA0', 'PISGB0', 'PISGC0', 'PISGD0', 'PISFA0')


def build_weapon(w, pal, gfx):
    tab, data = [], Blob()
    for nm in WEAPONS:
        lump = w.get(nm)
        if lump is None:
            tab.append((1, 1, 0, 0, 0)); continue
        pw, ph, xo, yo, px = W.patch_to_indices(lump)
        col = bytearray(pw * ph)
        for x in range(pw):
            for y in range(ph):
                c = px[y * pw + x]
                col[x * ph + y] = 255 if c is None else c
        off = data.add(bytes(col), 1)
        tab.append((pw, ph, xo, yo, off))
    t_off = gfx.add(b''.join(struct.pack('<HHhhI', *t) for t in tab))
    d_off = gfx.add(bytes(data.b))
    print(f'  оружие: {len(tab)} кадров, {len(data.b)} Б')
    return t_off, d_off, len(tab)


# ---------------------------------------------------------------------------
#  музыка: MIDI -> одноголосая мелодия (у нас квадратная волна на одной ножке)
# ---------------------------------------------------------------------------
def midi_melody(data, max_notes=420, max_ms=90000):
    if data[:4] != b'MThd':
        return []
    ntrk, div = struct.unpack_from('>HH', data, 10)
    pos = 8 + struct.unpack_from('>I', data, 4)[0]
    events = []                       # (тик, приоритет, вид, нота)
    tempos = [(0, 500000)]
    for _ in range(ntrk):
        if pos + 8 > len(data) or data[pos:pos + 4] != b'MTrk':
            break
        ln = struct.unpack_from('>I', data, pos + 4)[0]
        p, end = pos + 8, pos + 8 + ln
        pos = end
        tick, status = 0, 0
        while p < end:
            d, p = read_var(data, p)
            tick += d
            b = data[p]
            if b & 0x80:
                status = b; p += 1
            cmd, ch = status & 0xF0, status & 0x0F
            if cmd in (0x80, 0x90):
                note, vel = data[p], data[p + 1]; p += 2
                if ch == 9:
                    continue          # ударные мимо: свист вместо барабана не нужен
                if cmd == 0x90 and vel:
                    events.append((tick, 1, note))
                else:
                    events.append((tick, 0, note))
            elif cmd in (0xA0, 0xB0, 0xE0):
                p += 2
            elif cmd in (0xC0, 0xD0):
                p += 1
            elif status == 0xFF:
                meta = data[p]; p += 1
                ln2, p = read_var(data, p)
                if meta == 0x51 and ln2 == 3:
                    tempos.append((tick, (data[p] << 16) | (data[p + 1] << 8) | data[p + 2]))
                p += ln2
            elif status in (0xF0, 0xF7):
                ln2, p = read_var(data, p)
                p += ln2
            else:
                break
    if not events:
        return []
    events.sort(key=lambda e: (e[0], e[1]))
    tempos.sort()

    def tick_ms(t):                   # тики -> миллисекунды с учётом смен темпа
        ms, last, cur = 0.0, 0, tempos[0][1]
        for tt, tempo in tempos[1:]:
            if tt >= t:
                break
            ms += (tt - last) * cur / div / 1000.0
            last, cur = tt, tempo
        return ms + (t - last) * cur / div / 1000.0

    out, active, prev_tick, prev_hz = [], {}, 0, 0
    for tick, kind, note in events:
        if tick != prev_tick:
            dur = tick_ms(tick) - tick_ms(prev_tick)
            hz = 0
            if active:
                top = max(active)
                hz = int(round(440.0 * (2.0 ** ((top - 69) / 12.0))))
            if dur >= 1:
                if out and out[-1][0] == hz:
                    out[-1] = (hz, out[-1][1] + dur)
                else:
                    out.append((hz, dur))
            prev_tick = tick
        if kind:
            active[note] = active.get(note, 0) + 1
        elif note in active:
            active[note] -= 1
            if active[note] <= 0:
                del active[note]
    res, total = [], 0.0
    for hz, ms in out:
        ms = min(int(round(ms)), 3000)
        if ms < 12:
            continue
        res.append((hz if hz >= 90 else 0, ms))
        total += ms
        if len(res) >= max_notes or total >= max_ms:
            break
    return res


def read_var(data, p):
    v = 0
    while True:
        b = data[p]; p += 1
        v = (v << 7) | (b & 0x7F)
        if not (b & 0x80):
            return v, p


def build_music(w, name, out_path):
    mel = midi_melody(w.get(name) or b'')
    nl = chr(10)
    with open(out_path, 'w') as f:
        f.write('/* сгенерировано tools/mkdoom.py из ' + name +
                ' (музыка Freedoom, свободная лицензия) */' + nl + '#include "snd.h"' + nl + nl)
        f.write('const snote doom_music[] = {')
        for i, (hz, ms) in enumerate(mel):
            if i % 8 == 0:
                f.write(nl + '    ')
            f.write('{%d,%d},' % (hz, ms))
        f.write(nl + '};' + nl)
        f.write('const int doom_music_len = %d;' % len(mel) + nl)
    total = sum(m for _, m in mel)
    print('  музыка %s: %d нот, %d с' % (name, len(mel), total // 1000))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--wad', required=True)
    ap.add_argument('--maps', nargs='+', default=['E1M1'])
    a = ap.parse_args()
    w = W.Wad(a.wad)
    pal = w.playpal(0)
    cmap = w.colormap()
    pnames = W.read_pnames(w)
    texdefs = W.read_textures(w)
    os.makedirs(OUT, exist_ok=True)

    # --- что вообще нужно этим картам -----------------------------------
    need_tex, need_flat = {}, {}
    tex_use, flat_use = {}, {}
    maps = []
    for mn in a.maps:
        sides = w.map_lump(mn, 'SIDEDEFS')
        secs = w.map_lump(mn, 'SECTORS')
        if not sides or not secs:
            sys.exit(f'нет карты {mn}')
        for i in range(len(sides) // 30):
            o = i * 30
            for k in (4, 12, 20):
                nm = sides[o + k:o + k + 8].split(b'\0')[0].decode('ascii', 'replace').upper()
                if nm and nm != '-':
                    need_tex.setdefault(nm, len(need_tex))
                    tex_use[nm] = tex_use.get(nm, 0) + 1
        for i in range(len(secs) // 26):
            o = i * 26
            for k in (4, 12):
                nm = secs[o + k:o + k + 8].split(b'\0')[0].decode('ascii', 'replace').upper()
                need_flat.setdefault(nm, len(need_flat))
                flat_use[nm] = flat_use.get(nm, 0) + 1
        maps.append(mn)
    for sky in ('SKY1', 'RSKY1'):
        if sky in texdefs:
            need_tex.setdefault(sky, len(need_tex))
    print(f'карт {len(maps)}, текстур {len(need_tex)}, флэтов {len(need_flat)}')

    # --- графика ---------------------------------------------------------
    gfx = Blob()
    gfx.b += b'ZDG2'
    hdr = len(gfx.b)
    gfx.b += struct.pack('<17I', *([0] * 17))   # заполним потом

    pal_off = gfx.add(b''.join(struct.pack('<H', rgb565_be(*pal[i])) for i in range(256)))
    cm_off = gfx.add(b''.join(bytes(cmap[i]) for i in range(32)) if cmap else bytes(32 * 256))

    # самым ходовым — по 64 пикселя на сторону, остальным по 32
    rank = sorted(need_tex, key=lambda n: -tex_use.get(n, 0))
    big, mid = set(rank[:24]), set(rank[24:84])
    cache = {}
    seen = {}
    tex_tab, tex_data = [], Blob()
    for nm, idx in sorted(need_tex.items(), key=lambda kv: kv[1]):
        TEXMAX = TEXBIG if nm in big else (TEXSMALL if nm in mid else 16)
        td = texdefs.get(nm)
        if td is None:
            tex_tab.append((8, 8, 0, 0, 0)); continue
        tw, th, px = W.compose_texture(w, td, pnames, cache)
        nw, nh = min(pow2(tw), TEXMAX), min(pow2(th), TEXMAX)
        sx, sy = 0, 0
        while (tw >> sx) > nw: sx += 1
        while (th >> sy) > nh: sy += 1
        small = scale_indices(px, tw, th, nw, nh, pal)
        # по столбцам: рисуем стену колонками, так кэш попадает
        col = bytearray(nw * nh)
        for x in range(nw):
            for y in range(nh):
                col[x * nh + y] = small[y * nw + x]
        key = bytes(col)
        off = seen.get(key)
        if off is None:
            off = tex_data.add(key, 1)
            seen[key] = off
        tex_tab.append((tw, th, nw, nh, off))
    tex_tab_off = gfx.add(b''.join(struct.pack('<HHHHI', *t) for t in tex_tab))
    tex_data_off = gfx.add(bytes(tex_data.b))

    flat_rank = sorted(need_flat, key=lambda n: -flat_use.get(n, 0))
    flat_big = set(flat_rank[:16])
    flat_tab, flat_data, fseen = [], Blob(), {}
    for nm, idx in sorted(need_flat.items(), key=lambda kv: kv[1]):
        n = FLATN if nm in flat_big else 16
        d = w.get(nm)
        if d is None or len(d) < 4096:
            flat_tab.append((flat_data.add(bytes(n * n), 1), n)); continue
        px = list(d[:4096])
        small = bytes(scale_indices(px, 64, 64, n, n, pal))
        o = fseen.get(small)
        if o is None:
            o = flat_data.add(small, 1); fseen[small] = o
        flat_tab.append((o, n))
    flat_tab_off = gfx.add(b''.join(struct.pack('<II', o, n) for o, n in flat_tab))
    flat_data_off = gfx.add(bytes(flat_data.b))

    all_types = set()
    for mn in maps:
        th = w.map_lump(mn, 'THINGS')
        for i in range(len(th) // 10):
            all_types.add(struct.unpack_from('<H', th, i * 10 + 6)[0])
    spr_tab, spr_data, spr_info, spr_n = build_sprites(w, pal, all_types, gfx)
    wpn_tab, wpn_data, wpn_n = build_weapon(w, pal, gfx)
    skyflat = need_flat.get('F_SKY1', 0xFFFF)
    skytex = need_tex.get('SKY1', need_tex.get('RSKY1', 0))
    struct.pack_into('<17I', gfx.b, hdr, pal_off, cm_off, tex_tab_off, tex_data_off,
                     len(tex_tab), flat_tab_off, flat_data_off, len(flat_tab),
                     skyflat, skytex, spr_tab, spr_data, spr_info, spr_n,
                     wpn_tab, wpn_data, wpn_n)
    print(f'  небо: флэт {skyflat}, текстура {skytex}')
    open(os.path.join(OUT, 'doom_gfx.bin'), 'wb').write(bytes(gfx.b))
    print(f'  графика: {len(gfx.b)} Б (текстуры {len(tex_data.b)}, флэты {len(flat_data.b)})')

    # --- карты -----------------------------------------------------------
    mb = Blob()
    mb.b += b'ZDM1'
    mb.b += struct.pack('<I', len(maps))
    dir_off = len(mb.b)
    mb.b += b'\0' * (len(maps) * 64)          # по карте: 8 лумпов x (смещение, счётчик)
    entries = []
    for mn in maps:
        verts = w.map_lump(mn, 'VERTEXES')
        lines = w.map_lump(mn, 'LINEDEFS')
        sides = w.map_lump(mn, 'SIDEDEFS')
        segs = w.map_lump(mn, 'SEGS')
        ssec = w.map_lump(mn, 'SSECTORS')
        nodes = w.map_lump(mn, 'NODES')
        secs = w.map_lump(mn, 'SECTORS')
        things = w.map_lump(mn, 'THINGS')

        side2 = bytearray()
        for i in range(len(sides) // 30):
            o = i * 30
            xo, yo = struct.unpack_from('<hh', sides, o)
            def tid(k):
                nm = sides[o + k:o + k + 8].split(b'\0')[0].decode('ascii', 'replace').upper()
                return need_tex.get(nm, 0xFFFF) if nm and nm != '-' else 0xFFFF
            sec = struct.unpack_from('<H', sides, o + 28)[0]
            side2 += struct.pack('<hhHHHH', xo, yo, tid(4), tid(12), tid(20), sec)

        sec2 = bytearray()
        for i in range(len(secs) // 26):
            o = i * 26
            fh, ch = struct.unpack_from('<hh', secs, o)
            def fid(k):
                nm = secs[o + k:o + k + 8].split(b'\0')[0].decode('ascii', 'replace').upper()
                return need_flat.get(nm, 0)
            light, special, tag = struct.unpack_from('<hhh', secs, o + 20)
            sec2 += struct.pack('<hhHHhhh', fh, ch, fid(4), fid(12), light, special, tag)

        e = {}
        for k, data, sz in (('vert', verts, 4), ('line', lines, 14), ('side', bytes(side2), 12),
                            ('seg', segs, 12), ('ssec', ssec, 4), ('node', nodes, 28),
                            ('sec', bytes(sec2), 14), ('thing', things, 10)):
            e[k] = (mb.add(data), len(data) // sz)
        entries.append(e)

    for i, e in enumerate(entries):
        vals = []
        for k in ('vert', 'line', 'side', 'seg', 'ssec', 'node', 'sec', 'thing'):
            vals += [e[k][0], e[k][1]]
        struct.pack_into('<16I', mb.b, dir_off + i * 64, *vals)
    open(os.path.join(OUT, 'doom_maps.bin'), 'wb').write(bytes(mb.b))
    print(f'  карты: {len(mb.b)} Б')

    build_music(w, 'D_' + maps[0], os.path.join(OUT, 'doom_music.c'))

    with open(os.path.join(OUT, 'doom_data.S'), 'w') as f:
        f.write('/* сгенерировано tools/mkdoom.py */\n')
        f.write('    .section .rodata.doom,"a"\n    .balign 4\n')
        # у Mach-O имена с подчёркиванием, у ELF — без: объявляем оба
        f.write('    .global doom_gfx\n    .global _doom_gfx\ndoom_gfx:\n_doom_gfx:\n'
                '    .incbin "build/doom_gfx.bin"\n')
        f.write('    .balign 4\n    .global doom_maps\n    .global _doom_maps\n'
                'doom_maps:\n_doom_maps:\n    .incbin "build/doom_maps.bin"\n')
    print(f'  всего: {len(gfx.b) + len(mb.b)} Б')


main()
