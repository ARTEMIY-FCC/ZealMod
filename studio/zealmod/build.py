"""Сборка образа ZealMod: стоковая прошивка + ядро + выбранные модули + тема.

Ничего не компилируется: ядро приходит готовым ELF, модули — объектными
файлами внутри .zm.  Studio раскладывает их по свободным адресам, правит
релокации, заполняет таблицу zm_tab и пересобирает образ.

Раскладка (см. work/tools/img.py — там она объяснена со стороны прошивки):

    IROM  [ стоковый код ][ трамплины ядра .ptext ]
    DROM  [ стоковые данные ][ страница кода 0 ][ .prodata ядра ]
          [ данные модулей ][ ещё страницы кода ]

Страницы кода лежат внутри сегмента данных и становятся исполняемыми потому,
что ядро отображает их ещё раз через свободные записи MMU.
"""
import time
from dataclasses import dataclass, field

from . import tab
from .elf import Elf32
from .image import AppImage, PART_SIZE, jal
from .link import Arena, LinkError, Linker

MMU_PAGE = 0x10000
PART_OFF = 0x100000          # ota_0: страницы флеша считаются отсюда
RTC_END = 0x50001F00         # хвост RTC оставлен прошивке


class BuildError(Exception):
    pass


@dataclass
class Module:
    """Разобранный .zm, готовый к укладке в образ."""
    id: str
    title: str
    obj: bytes
    cover: bytes = b''
    pal: bytes = b''
    blobs: dict = field(default_factory=dict)
    names: dict = field(default_factory=dict)     # подпись по языкам
    entry: str = 'zm_main'
    exit_btn: int = 0
    exit_hold: int = 14
    abi: int = 1
    version: str = ''
    author: str = ''
    source: str = ''


@dataclass
class Report:
    size: int = 0
    free: int = 0
    code: int = 0
    data: int = 0
    bss: int = 0
    pages: int = 0
    modules: list = field(default_factory=list)
    warnings: list = field(default_factory=list)


class Region:
    """Кусок памяти, который растёт по мере записи по абсолютным адресам."""

    def __init__(self, base):
        self.base = base
        self.buf = bytearray()

    def write(self, addr, data):
        o = addr - self.base
        if o < 0:
            raise BuildError(f'write below the region start: {addr:#x} < {self.base:#x}')
        if o + len(data) > len(self.buf):
            self.buf += b'\0' * (o + len(data) - len(self.buf))
        self.buf[o:o + len(data)] = data

    @property
    def end(self):
        return self.base + len(self.buf)


def core_layout(img: AppImage):
    """Где в этом образе начинается наш код и наши данные."""
    dva, dd = img.segs[img.drom]
    ddata_off = 24 + 8 + sum(8 + len(s[1]) for s in img.segs[:img.drom])
    iva, idd = img.segs[img.irom]
    flash = lambda va: PART_OFF + ddata_off + (va - dva)
    text = (iva + len(idd) + 15) & ~15
    limit = 0x42000000 + ((dva & 0x7FFFFF) & ~(MMU_PAGE - 1))
    end = (dva + len(dd) + 15) & ~15
    pt2 = end + (-flash(end)) % MMU_PAGE
    return dict(text=text, text_len=limit - text, text2=0x42000000 + 40 * MMU_PAGE,
                entry=40, page=flash(pt2) // MMU_PAGE, pt2_lma=pt2,
                data=pt2 + MMU_PAGE, flash=flash, drom_va=dva)


def build(base: bytes, core_elf: bytes, modules, theme=None, cfg=None,
          profile=None, build_str=None, check_base=True, theme_assets=None):
    """Собрать образ. Возвращает (bytes, Report)."""
    profile = profile or {}
    theme = dict(tab.DEFAULT_THEME, **(theme or {}))
    cfg = dict(cfg or {})
    lang = str(cfg.get('lang', 'en')).lower()     # на нём подписи в меню
    img = AppImage(base)
    core = Elf32(core_elf, 'core.elf')
    rep = Report()

    L = core_layout(img)
    sec = {s.name: s for s in core.sections}
    for need in ('.ptext', '.ptext2', '.prodata'):
        if need not in sec:
            raise BuildError(f'the core has no {need} section')
    ptext, ptext2, prodata = sec['.ptext'], sec['.ptext2'], sec['.prodata']
    for s, want, what in ((ptext, L['text'], 'trampolines'),
                          (ptext2, L['text2'], 'core code'),
                          (prodata, L['data'], 'core data')):
        if s.size and s.addr != want:
            raise BuildError(f'{what}: the core is linked at {s.addr:#x}, the image needs '
                             f'{want:#x} — core and stock firmware do not match')
    if len(ptext.data) > L['text_len']:
        raise BuildError('trampolines do not fit the tail of the stock IROM')
    if len(ptext2.data) > MMU_PAGE:
        raise BuildError('core code is larger than one MMU page')

    exports = {s.name: s.value for s in core.symbols
               if s.defined and s.global_ and s.name}
    for must in ('zm_tab', 'zg_hook_boot'):
        if must not in exports:
            raise BuildError(f'the core has no symbol {must}')

    # --- арены -------------------------------------------------------------
    max_pages = int(profile.get('max_code_pages', 8))
    free_now = img.free_bytes()
    code = Arena('module code', (L['text2'] + len(ptext2.data) + 3) & ~3,
                 L['text2'] + max_pages * MMU_PAGE)
    data = Arena('module data', (prodata.addr + prodata.size + 15) & ~15,
                 prodata.addr + prodata.size + max(0, free_now))
    persist_end = exports.get('__persist_end__')
    if persist_end is None:
        raise BuildError('the core has no __persist_end__ symbol')
    bss = Arena('RTC memory', (persist_end + 7) & ~7, RTC_END)
    linker = Linker(exports, code, data, bss)

    # --- картинки темы: обои и логотип заставки ----------------------------
    rod = Region(prodata.addr)
    rod.write(prodata.addr, prodata.data)
    ta = theme_assets or {}
    if ta.get('wallpaper'):
        at = data.take(len(ta['wallpaper']), 4)
        rod.write(at, ta['wallpaper'])
        theme['wallpaper'] = at
        theme['flags'] = int(theme.get('flags', 0)) | 1
    if ta.get('logo') and ta.get('logo_pal'):
        at = data.take(len(ta['logo']), 4)
        rod.write(at, ta['logo'])
        pat = data.take(len(ta['logo_pal']), 4)
        rod.write(pat, ta['logo_pal'])
        theme['logo'] = at
        theme['logo_pal'] = pat
        theme['logo_w'] = ta.get('logo_w', 0)
        theme['logo_h'] = ta.get('logo_h', 0)

    # --- модули ------------------------------------------------------------
    chunks = []                      # (адрес, байты) — код модулей
    mods = []
    for m in modules:
        if m.abi > tab.ABI:
            raise BuildError(f'{m.id}: the module asks for ABI {m.abi}, the core provides {tab.ABI}')
        extern = {}
        for name, blob in sorted(m.blobs.items()):
            at = data.take(len(blob), 16)
            rod.write(at, blob)
            extern[name] = at
        title = (m.names.get(lang) or m.title).encode('utf-8') + b'\0'
        t_at = data.take(len(title), 4)
        rod.write(t_at, title)
        c_at = p_at = 0
        if m.cover and m.pal:
            c_at = data.take(len(m.cover), 4)
            rod.write(c_at, m.cover)
            p_at = data.take(len(m.pal), 4)
            rod.write(p_at, m.pal)
        try:
            lk = linker.link(Elf32(m.obj, m.id), m.id, extern)
        except (LinkError, ValueError) as e:
            raise BuildError(f'{m.id}: {e}') from None
        if m.entry not in lk.symbols:
            raise BuildError(f'{m.id}: the module has no {m.entry}() function')
        for at, blob in lk.chunks:
            if at >= L['text2']:
                chunks.append((at, blob))
            else:
                rod.write(at, blob)
        mods.append(dict(run=lk.symbols[m.entry], title=t_at, cover=c_at, pal=p_at,
                         id=tab.mod_id(m.id), flags=0, exit_btn=m.exit_btn,
                         exit_hold=m.exit_hold))
        rep.modules.append(dict(id=m.id, title=m.names.get(lang) or m.title, code=lk.text_bytes,
                                data=lk.rodata_bytes + len(title) + len(m.cover) +
                                len(m.pal) + sum(len(b) for b in m.blobs.values()),
                                bss=lk.bss_bytes, imports=len(lk.imports)))

    # --- сколько страниц кода понадобилось --------------------------------
    code_used = code.at - L['text2']
    pages = max(1, (code_used + MMU_PAGE - 1) // MMU_PAGE)
    if pages > max_pages:
        raise BuildError(f'module code needs more than {max_pages} MMU pages')

    # --- собираем сегмент данных ------------------------------------------
    dva, dd = img.segs[img.drom]
    pad = L['pt2_lma'] - (dva + len(dd))
    if pad < 0:
        raise BuildError('the data segment already overruns the code area')
    dd += b'\0' * pad
    page0_at = len(dd)
    dd += b'\0' * MMU_PAGE
    dd[page0_at:page0_at + len(ptext2.data)] = ptext2.data
    rod_at = len(dd)
    dd += rod.buf
    # хвост данных добиваем до страницы флеша, дальше — остальные страницы кода
    extra_va = dva + len(dd)
    extra_pad = (-L['flash'](extra_va)) % MMU_PAGE
    dd += b'\0' * extra_pad
    extra_at = len(dd)
    dd += b'\0' * ((pages - 1) * MMU_PAGE)

    def put_code(at, blob):
        """Код лежит в данных: виртуальный адрес -> место в сегменте."""
        while blob:
            k = (at - L['text2']) // MMU_PAGE
            off = (at - L['text2']) % MMU_PAGE
            n = min(len(blob), MMU_PAGE - off)
            base = page0_at if k == 0 else extra_at + (k - 1) * MMU_PAGE
            dd[base + off:base + off + n] = blob[:n]
            blob = blob[n:]
            at += n

    for at, blob in chunks:
        put_code(at, blob)

    # --- таблица ZealMod ---------------------------------------------------
    mmu = []
    at = L['flash'](dva + extra_at)          # смещение во флеше -> номер страницы
    if at % MMU_PAGE:
        raise BuildError('code pages did not land on a flash page boundary')
    first_page = at // MMU_PAGE
    for k in range(1, pages):
        mmu.append((L['entry'] + k, first_page + (k - 1)))
    table = tab.pack_tab(mmu=mmu, cfg=cfg, theme=theme, mods=mods,
                         bss_end=bss.at if bss.at > bss.base else 0,
                         build=build_str or time.strftime('%Y-%m-%d'))
    tab_off = rod_at + (exports['zm_tab'] - prodata.addr)   # адрес -> место в сегменте
    if not (rod_at <= tab_off < len(dd)):
        raise BuildError('the zm_tab table ended up outside the data segment')
    dd[tab_off:tab_off + len(table)] = table

    # --- трамплины и хуки --------------------------------------------------
    _, idd = img.segs[img.irom]
    idd += b'\0' * ((-len(idd)) % 16)
    if img.segs[img.irom][0] + len(idd) != ptext.addr:
        raise BuildError('core trampolines do not sit right after the stock code')
    idd += ptext.data
    idd += b'\0' * ((-len(idd)) % 4)

    for h in profile.get('hooks', []):
        at = int(str(h['addr']), 0)
        sym = h['symbol']
        if sym not in exports:
            raise BuildError(f'the core has no trampoline {sym}')
        if check_base and h.get('expect'):
            was = img.read(at, 4).hex()
            if was != h['expect'].replace(' ', '').lower():
                raise BuildError(f'at {at:#x} the firmware holds {was}, the mod expected '
                                 f'{h["expect"]} — this is a different firmware version')
        img.write(at, jal(at, exports[sym]))

    image = img.build()
    rep.size = len(image)
    rep.free = PART_SIZE - len(image)
    rep.code = code_used
    rep.data = data.at - data.base
    rep.bss = bss.at - bss.base
    rep.pages = pages
    if rep.bss and bss.at > RTC_END:
        raise BuildError('not enough RTC memory for the modules')
    return image, rep
