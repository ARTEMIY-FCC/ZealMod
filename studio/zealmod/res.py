"""Где Studio берёт ядро, стоковый образ, модули и темы.

Работает и из дерева разработчика (тогда всё лежит в work/ и dist/), и из
собранного комплекта, где рядом с программой просто папка bundle/.
"""
import hashlib
import json
import os
from pathlib import Path

HERE = Path(__file__).resolve().parent
PKG_PROFILES = HERE / 'profiles'


def _first(*paths):
    for p in paths:
        if p and Path(p).exists():
            return Path(p)
    return None


# по этим приметам каталог узнаётся как комплект ZealMod
MARKS = ('dist/core/zealmod-core.elf', 'dist/modules', 'modules/builtin.json',
         'work/src/plat.h')


def _is_kit(p: Path):
    return any((p / m).exists() for m in MARKS)


def _find_root():
    """Где лежит комплект: рядом с программой или выше по дереву от текущего
    каталога — чтобы `zealmod` работал и из подпапки, и будучи установленным."""
    near = HERE.parents[1]
    if _is_kit(near):
        return near
    cwd = Path.cwd().resolve()
    for d in (cwd, *cwd.parents):
        if _is_kit(d):
            return d
    return near


class Bundle:
    """Комплект: стоковый образ + ядро + профиль + каталоги модулей и тем."""

    def __init__(self, root=None):
        env = os.environ.get('ZEALMOD_BUNDLE')
        self.root = Path(root or env or _find_root()).resolve()
        self.dist = self.root / 'dist'
        self.fw = self.root / 'work'

    # --- файлы -----------------------------------------------------------
    @property
    def base(self):
        p = _first(self.dist / 'base' / 'zeal-stock.gbl', self.root / 'original.gbl')
        if not p:
            raise FileNotFoundError('stock image not found (dist/base/zeal-stock.gbl or original.gbl)')
        return p

    @property
    def core(self):
        p = _first(self.dist / 'core' / 'zealmod-core.elf', self.fw / 'build' / 'core.elf')
        if not p:
            raise FileNotFoundError('core not found (dist/core/zealmod-core.elf); build it: cd work && make core')
        return p

    @property
    def sdk(self):
        """Заголовки, по которым собираются модули."""
        return _first(self.dist / 'sdk' / 'include', self.root / 'sdk' / 'include') \
            or self.dist / 'sdk' / 'include'

    @property
    def modules_dir(self):
        return _first(self.dist / 'modules', self.root / 'modules') or self.dist / 'modules'

    @property
    def themes_dir(self):
        return _first(self.dist / 'themes', self.root / 'themes') or self.dist / 'themes'

    def profile(self, pid='zeal-v1'):
        p = _first(self.dist / 'profiles' / f'{pid}.json', PKG_PROFILES / f'{pid}.json')
        if not p:
            raise FileNotFoundError(f'no device profile {pid}')
        return json.loads(p.read_text('utf-8'))

    # --- содержимое ------------------------------------------------------
    def modules(self):
        d = self.modules_dir
        return sorted(d.glob('*.zm')) if d.exists() else []

    def themes(self):
        d = self.themes_dir
        return sorted(d.glob('*.zt')) if d.exists() else []

    def base_ok(self, profile=None):
        """Совпадает ли стоковый образ с тем, под который собран мод."""
        profile = profile or self.profile()
        want = profile.get('base_sha256')
        got = hashlib.sha256(self.base.read_bytes()).hexdigest()
        return (got == want, got, want)
