"""Minimal WAD reader: directory, lumps, palette, patches, textures, flats, maps.

Written against the original id Software file formats (public documentation),
so it reads Freedoom, shareware DOOM and any PWAD built for them.
"""

import struct
from collections import OrderedDict

MAP_LUMPS = ('THINGS', 'LINEDEFS', 'SIDEDEFS', 'VERTEXES', 'SEGS',
             'SSECTORS', 'NODES', 'SECTORS', 'REJECT', 'BLOCKMAP',
             'BEHAVIOR')


def _name(raw):
    return raw.split(b'\0', 1)[0].decode('ascii', 'replace').upper()


class Wad:
    def __init__(self, path):
        with open(path, 'rb') as f:
            self.data = f.read()
        magic, count, dirofs = struct.unpack_from('<4sii', self.data, 0)
        if magic not in (b'IWAD', b'PWAD'):
            raise ValueError('not a WAD: %r' % magic)
        self.kind = magic.decode()
        self.lumps = []            # [(name, offset, size)]
        self.index = OrderedDict()  # name -> last index (lumps repeat)
        for i in range(count):
            ofs, size, raw = struct.unpack_from('<ii8s', self.data, dirofs + 16 * i)
            nm = _name(raw)
            self.lumps.append((nm, ofs, size))
            self.index.setdefault(nm, i)

    # -- raw access ------------------------------------------------------
    def lump(self, i):
        _, ofs, size = self.lumps[i]
        return self.data[ofs:ofs + size]

    def get(self, name, default=None):
        i = self.index.get(name.upper())
        if i is None:
            return default
        return self.lump(i)

    def find(self, name, start=0):
        name = name.upper()
        for i in range(start, len(self.lumps)):
            if self.lumps[i][0] == name:
                return i
        return -1

    def between(self, start_marker, end_marker):
        """Lump names between two markers, e.g. F_START/F_END."""
        a = self.find(start_marker)
        b = self.find(end_marker)
        out = []
        if a < 0 or b < 0:
            return out
        for i in range(a + 1, b):
            nm, ofs, size = self.lumps[i]
            if nm.endswith('_START') or nm.endswith('_END'):
                continue
            out.append((nm, i))
        return out

    # -- palette ---------------------------------------------------------
    def playpal(self, which=0):
        pal = self.get('PLAYPAL')
        base = 768 * which
        return [tuple(pal[base + 3 * i: base + 3 * i + 3]) for i in range(256)]

    def colormap(self):
        """34 colormaps of 256 palette indices (0 = brightest, 31 = black)."""
        cm = self.get('COLORMAP')
        if not cm:
            return None
        return [list(cm[256 * i:256 * i + 256]) for i in range(34)]

    # -- maps ------------------------------------------------------------
    def maps(self):
        """Ordered list of map marker names present (ExMy / MAPxx)."""
        out = []
        for i, (nm, _, _) in enumerate(self.lumps):
            is_map = (len(nm) == 4 and nm[0] == 'E' and nm[2] == 'M' and
                      nm[1].isdigit() and nm[3].isdigit())
            is_map = is_map or (len(nm) == 5 and nm.startswith('MAP') and nm[3:].isdigit())
            if is_map and i + 1 < len(self.lumps) and self.lumps[i + 1][0] in MAP_LUMPS:
                out.append(nm)
        return out

    def map_lump(self, mapname, lumpname):
        i = self.find(mapname)
        if i < 0:
            return None
        for j in range(i + 1, min(i + 12, len(self.lumps))):
            nm, ofs, size = self.lumps[j]
            if nm == lumpname:
                return self.data[ofs:ofs + size]
            if nm not in MAP_LUMPS:
                break
        return None


# ---------------------------------------------------------------------------
#  graphics
# ---------------------------------------------------------------------------

def patch_to_indices(data):
    """Decode a DOOM picture lump into palette indices.

    Returns (w, h, xoff, yoff, pixels) with pixels row-major, each entry a
    palette index or None for transparent."""
    w, h, xoff, yoff = struct.unpack_from('<hhhh', data, 0)
    if w <= 0 or h <= 0 or w > 4096 or h > 4096:
        raise ValueError('bad patch header %dx%d' % (w, h))
    colofs = struct.unpack_from('<%dI' % w, data, 8)
    px = [None] * (w * h)
    for x in range(w):
        ofs = colofs[x]
        if ofs >= len(data):
            continue
        prev_top = -1
        while True:
            top = data[ofs]
            if top == 0xff:
                break
            cnt = data[ofs + 1]
            # tall-patch support: relative topdelta when it goes backwards
            if top <= prev_top:
                top = prev_top + top
            prev_top = top
            src = ofs + 3
            for k in range(cnt):
                y = top + k
                if 0 <= y < h:
                    px[y * w + x] = data[src + k]
            ofs = src + cnt + 1
    return w, h, xoff, yoff, px


def flat_to_indices(data, size=64):
    return list(data[:size * size])


def read_pnames(wad):
    d = wad.get('PNAMES')
    n = struct.unpack_from('<i', d, 0)[0]
    return [_name(d[4 + 8 * i: 12 + 8 * i]) for i in range(n)]


def read_textures(wad):
    """name -> dict(width, height, patches=[(originx, originy, pnameIdx)])"""
    out = OrderedDict()
    for lname in ('TEXTURE1', 'TEXTURE2'):
        d = wad.get(lname)
        if not d:
            continue
        count = struct.unpack_from('<i', d, 0)[0]
        offs = struct.unpack_from('<%di' % count, d, 4)
        for o in offs:
            # maptexture_t: name[8] masked:i32 w:i16 h:i16 coldir:i32 npatch:i16
            nm = _name(d[o:o + 8])
            w, h = struct.unpack_from('<hh', d, o + 12)
            npatch = struct.unpack_from('<h', d, o + 20)[0]
            patches = []
            for i in range(npatch):
                px, py, pi = struct.unpack_from('<hhh', d, o + 22 + 10 * i)
                patches.append((px, py, pi))
            out[nm] = {'w': w, 'h': h, 'patches': patches}
    return out


def compose_texture(wad, tex, pnames, cache=None):
    """Compose a wall texture from its patches -> (w, h, palette indices)."""
    w, h = tex['w'], tex['h']
    px = [None] * (w * h)
    for ox, oy, pi in tex['patches']:
        if pi >= len(pnames):
            continue
        pname = pnames[pi]
        key = ('P', pname)
        if cache is not None and key in cache:
            pw, ph, ppx = cache[key]
        else:
            lump = wad.get(pname)
            if lump is None or len(lump) < 8:
                continue
            try:
                pw, ph, _, _, ppx = patch_to_indices(lump)
            except Exception:
                continue
            if cache is not None:
                cache[key] = (pw, ph, ppx)
        for y in range(ph):
            ty = oy + y
            if ty < 0 or ty >= h:
                continue
            row = y * pw
            trow = ty * w
            for x in range(pw):
                tx = ox + x
                if tx < 0 or tx >= w:
                    continue
                c = ppx[row + x]
                if c is not None:
                    px[trow + tx] = c
    return w, h, px
