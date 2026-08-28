"""ZealMod Studio — окно, в котором собирают свою прошивку.

Программа поднимает у себя на компьютере маленький сервер и открывает браузер:
так одинаково работает и на macOS, и на Windows, и на Linux, и ничего, кроме
Python, ставить не нужно.  Наружу ничего не слушает — только 127.0.0.1.
"""
import io
import json
import mimetypes
import threading
import time
import traceback
import webbrowser
import zipfile
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

from .. import __version__, pack, tab
from ..build import BuildError, build
from ..elf import Elf32

WEB = Path(__file__).resolve().parent / 'web'


class Job:
    """Долгая операция (заливка, копия флеша) — чтобы окно не замирало."""

    def __init__(self):
        self.lines = []
        self.done = False
        self.ok = False
        self.title = ''
        self.lock = threading.Lock()

    def log(self, s):
        with self.lock:
            self.lines.append(s)
            del self.lines[:-400]

    def snapshot(self):
        with self.lock:
            return dict(lines=list(self.lines), done=self.done, ok=self.ok,
                        title=self.title)


class Studio:
    """Всё состояние окна: что за комплект, что собрано, что сейчас делается."""

    def __init__(self, bundle, profile='zeal-v1'):
        self.b = bundle
        self.profile_id = profile
        self.image = None            # последний собранный образ
        self.report = None
        self.job = None
        self.lib = {}                # id -> (Module, manifest, путь)
        self.reload()

    # --- библиотека --------------------------------------------------------
    def reload(self):
        self.profile = self.b.profile(self.profile_id)
        self.lib.clear()
        for p in self.b.modules():
            try:
                m, man = pack.read_module(p)
            except pack.PackError as e:
                self.lib[p.stem] = (None, dict(id=p.stem, name=p.stem,
                                               error=str(e)), p)
                continue
            self.lib[m.id] = (m, man, p)
        core = Elf32(self.b.core.read_bytes(), 'core.elf')
        self.exports = {s.name: s.value for s in core.symbols
                        if s.defined and s.global_ and s.name}

    def order(self):
        spec = self.b.root / 'modules' / 'builtin.json'
        order = []
        if spec.exists():
            try:
                order = [m['id'] for m in json.loads(spec.read_text('utf-8'))['modules']]
            except (KeyError, ValueError):
                pass
        known = [i for i in order if i in self.lib]
        return known + sorted(set(self.lib) - set(known))

    def modules_json(self):
        out = []
        for mid in self.order():
            m, man, path = self.lib[mid]
            if m is None:
                out.append(dict(id=mid, name=man.get('name', mid), broken=True,
                                problems=[man.get('error', 'не читается')]))
                continue
            chk = pack.check_module(m, self.exports)
            out.append(dict(id=m.id, name=m.title, version=man.get('version', ''),
                            author=man.get('author', ''), kind=man.get('kind', 'app'),
                            description=man.get('description', ''),
                            exit_button=man.get('exit_button', 'up'),
                            exit_hold=man.get('exit_hold', 1.4),
                            code=chk['code'], data=chk['data'], bss=chk['bss'],
                            size=path.stat().st_size, ok=chk['ok'],
                            problems=chk['problems'], builtin=path.parent ==
                            self.b.modules_dir and man.get('author') == 'ZealMod'))
        return out

    def themes_json(self):
        out = [dict(id='__default__', name='ZealMod', builtin=True,
                    colors=self.theme_colors(tab.DEFAULT_THEME), layout=0)]
        for p in self.b.themes():
            try:
                t, _assets, src = pack.read_theme(p)
            except pack.PackError as e:
                out.append(dict(id=p.stem, name=p.stem, error=str(e)))
                continue
            out.append(dict(id=p.stem, name=t['name'], builtin=False,
                            author=src.get('author', ''), layout=t['layout'],
                            colors=self.theme_colors(t)))
        return out

    @staticmethod
    def theme_colors(t):
        return {k: tab.hex_color(t[k]) for k in
                ('bg_top', 'bg_bot', 'fl_top', 'fl_bot', 'line', 'accent', 'text',
                 'text_dim', 'shadow')}

    def theme(self, tid):
        if not tid or tid == '__default__':
            return dict(tab.DEFAULT_THEME), {}
        p = self.b.themes_dir / f'{tid}.zt'
        if not p.exists():
            raise BuildError(f'нет темы {tid}')
        t, assets, _src = pack.read_theme(p)
        return t, assets

    # --- сборка ------------------------------------------------------------
    def compose(self, req):
        ids = req.get('modules') or self.order()
        mods = []
        for mid in ids:
            item = self.lib.get(mid)
            if not item or item[0] is None:
                raise BuildError(f'нет модуля {mid}')
            mods.append(item[0])
        theme, assets = self.theme(req.get('theme'))
        if req.get('layout') is not None:
            theme['layout'] = int(req['layout'])
        cfg = req.get('cfg') or {}
        ok, got, want = self.b.base_ok(self.profile)
        image, rep = build(self.b.base.read_bytes(), self.b.core.read_bytes(), mods,
                           theme=theme, cfg=cfg, profile=self.profile,
                           theme_assets=assets, check_base=ok)
        self.image, self.report = image, rep
        return dict(size=rep.size, free=rep.free, code=rep.code, data=rep.data,
                    bss=rep.bss, pages=rep.pages, modules=rep.modules,
                    base_ok=ok, base_sha=got, base_want=want)

    # --- устройство --------------------------------------------------------
    def devices(self):
        from .. import device
        return dict(ports=device.ports(), esptool=device.esptool_ok())

    def start_job(self, title, fn):
        if self.job and not self.job.done:
            raise BuildError('одно дело за раз: подождите, пока закончится текущее')
        job = Job()
        job.title = title
        self.job = job

        def run():
            try:
                fn(job)
                job.ok = True
            except Exception as e:                      # показываем как есть
                job.log(f'! {e}')
            finally:
                job.done = True
        threading.Thread(target=run, daemon=True).start()
        return job

    def flash(self, port):
        from .. import device
        if self.image is None:
            raise BuildError('сначала соберите образ')
        tmp = self.b.dist / 'zealmod.gbl'
        tmp.parent.mkdir(parents=True, exist_ok=True)
        tmp.write_bytes(self.image)

        def work(job):
            job.log(f'образ {len(self.image)} байт -> {port or "первые попавшиеся часы"}')
            device.flash(tmp, port, on_line=job.log)
            job.log('готово: часы перезагрузятся сами')
        return self.start_job('Заливка', work)

    def backup(self, port):
        from .. import device
        out = self.b.dist / f'zeal-backup-{time.strftime("%Y%m%d-%H%M")}.bin'

        def work(job):
            job.log(f'снимаю копию флеша в {out}')
            device.backup(out, port, on_line=job.log)
            job.log(f'копия сохранена: {out}')
        return self.start_job('Копия флеша', work)

    def restore_stock(self, port):
        from .. import device

        def work(job):
            job.log('возвращаю заводскую прошивку')
            device.flash(self.b.base, port, on_line=job.log)
            job.log('готово')
        return self.start_job('Заводская прошивка', work)

    # --- импорт ------------------------------------------------------------
    def import_file(self, name, data):
        kind = 'module' if name.lower().endswith('.zm') else \
               'theme' if name.lower().endswith('.zt') else None
        if not kind:
            raise BuildError('понимаю только .zm (программы) и .zt (темы)')
        try:
            zipfile.ZipFile(io.BytesIO(data)).testzip()
        except zipfile.BadZipFile:
            raise BuildError('файл повреждён') from None
        d = self.b.modules_dir if kind == 'module' else self.b.themes_dir
        d.mkdir(parents=True, exist_ok=True)
        p = d / Path(name).name
        p.write_bytes(data)
        try:
            if kind == 'module':
                m, _man = pack.read_module(p)
                chk = pack.check_module(m, self.exports)
                self.reload()
                return dict(kind=kind, id=m.id, name=m.title, ok=chk['ok'],
                            problems=chk['problems'])
            pack.read_theme(p)
            self.reload()
            return dict(kind=kind, id=p.stem, ok=True, problems=[])
        except pack.PackError as e:
            p.unlink(missing_ok=True)
            raise BuildError(str(e)) from None


class Handler(BaseHTTPRequestHandler):
    studio: Studio = None
    server_version = 'ZealMod/' + __version__

    def log_message(self, *a):
        pass                                   # не сорим в консоль

    # --- ответы ------------------------------------------------------------
    def send_json(self, obj, code=200):
        body = json.dumps(obj, ensure_ascii=False).encode('utf-8')
        self.send_response(code)
        self.send_header('Content-Type', 'application/json; charset=utf-8')
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Cache-Control', 'no-store')
        self.end_headers()
        self.wfile.write(body)

    def send_bytes(self, data, ctype, filename=None):
        self.send_response(200)
        self.send_header('Content-Type', ctype)
        self.send_header('Content-Length', str(len(data)))
        if filename:
            self.send_header('Content-Disposition',
                             f'attachment; filename="{filename}"')
        self.end_headers()
        self.wfile.write(data)

    # --- маршруты ----------------------------------------------------------
    def do_GET(self):
        u = urlparse(self.path)
        q = parse_qs(u.query)
        s = self.studio
        try:
            if u.path in ('/', '/index.html'):
                return self.send_file(WEB / 'index.html')
            if u.path.startswith('/static/'):
                return self.send_file(WEB / u.path[len('/static/'):])
            if u.path == '/api/state':
                ok, got, want = s.b.base_ok(s.profile)
                return self.send_json(dict(
                    version=__version__, bundle=str(s.b.root),
                    profile=dict(id=s.profile['id'], name=s.profile['name'],
                                 screen=s.profile.get('screen', [240, 240])),
                    base=dict(ok=ok, sha=got, want=want, name=s.b.base.name),
                    modules=s.modules_json(), themes=s.themes_json(),
                    device=s.devices(), part=2 * 1024 * 1024))
            if u.path == '/api/devices':
                return self.send_json(s.devices())
            if u.path == '/api/progress':
                return self.send_json(s.job.snapshot() if s.job else
                                      dict(lines=[], done=True, ok=True, title=''))
            if u.path.startswith('/api/cover/'):
                mid = u.path[len('/api/cover/'):].removesuffix('.png')
                item = s.lib.get(mid)
                png = pack.module_preview(item[2]) if item else b''
                if not png:
                    return self.send_error(404)
                return self.send_bytes(png, 'image/png')
            if u.path == '/api/image':
                if s.image is None:
                    return self.send_error(409, 'образ ещё не собран')
                name = q.get('name', ['zealmod.gbl'])[0]
                return self.send_bytes(s.image, 'application/octet-stream', name)
            return self.send_error(404)
        except Exception as e:
            traceback.print_exc()
            return self.send_json(dict(error=str(e)), 500)

    def do_POST(self):
        u = urlparse(self.path)
        q = parse_qs(u.query)
        s = self.studio
        n = int(self.headers.get('Content-Length') or 0)
        raw = self.rfile.read(n) if n else b''
        try:
            if u.path == '/api/import':
                name = q.get('name', ['файл'])[0]
                return self.send_json(s.import_file(name, raw))
            req = json.loads(raw.decode('utf-8')) if raw else {}
            if u.path == '/api/build':
                return self.send_json(s.compose(req))
            if u.path == '/api/flash':
                s.flash(req.get('port'))
                return self.send_json(dict(started=True))
            if u.path == '/api/backup':
                s.backup(req.get('port'))
                return self.send_json(dict(started=True))
            if u.path == '/api/stock':
                s.restore_stock(req.get('port'))
                return self.send_json(dict(started=True))
            if u.path == '/api/reload':
                s.reload()
                return self.send_json(dict(ok=True))
            if u.path == '/api/remove':
                mid = req.get('id', '')
                item = s.lib.get(mid)
                if item:
                    Path(item[2]).unlink(missing_ok=True)
                    s.reload()
                return self.send_json(dict(ok=True))
            return self.send_error(404)
        except (BuildError, pack.PackError) as e:
            return self.send_json(dict(error=str(e)), 400)
        except Exception as e:
            traceback.print_exc()
            return self.send_json(dict(error=str(e)), 500)

    def send_file(self, p: Path):
        p = Path(p).resolve()
        if WEB not in p.parents:              # наружу из web/ не выпускаем
            return self.send_error(403)
        if not p.is_file():
            return self.send_error(404)
        ctype = mimetypes.guess_type(p.name)[0] or 'application/octet-stream'
        if ctype.startswith('text/') or ctype.endswith('javascript'):
            ctype += '; charset=utf-8'
        return self.send_bytes(p.read_bytes(), ctype)


def serve(bundle, port=8777, open_browser=True, profile='zeal-v1'):
    Handler.studio = Studio(bundle, profile)
    httpd = ThreadingHTTPServer(('127.0.0.1', port), Handler)
    url = f'http://127.0.0.1:{port}/'
    print(f'ZealMod Studio {__version__}')
    print(f'  комплект: {bundle.root}')
    print(f'  открыто:  {url}   (Ctrl+C чтобы закрыть)')
    if open_browser:
        threading.Timer(0.4, lambda: webbrowser.open(url)).start()
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print('\nдо встречи')
    finally:
        httpd.server_close()
