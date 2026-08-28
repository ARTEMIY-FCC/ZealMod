#!/usr/bin/env python3
"""Пульт к таймеру по USB Serial/JTAG.

  python3 tools/timer.py log [сек]            слушать консоль
  python3 tools/timer.py cmd m u u p          слать команды (m меню, x выход,
                                              0..9/a пункт, udlr «нажать»,
                                              UDLR долгое нажатие, p снимок)
  python3 tools/timer.py shot build/x.png     снять кадр экрана
  python3 tools/timer.py reset                перезагрузить часы
Между командами можно ставить паузу числом: `cmd m 1.5 p`.
"""
import glob, re, sys, time
import serial

W = H = 240


def port():
    p = sorted(glob.glob('/dev/cu.usbmodem*'))
    if not p:
        sys.exit('таймер не виден: подключи USB')
    return p[0]


def open_port(reset=False, drain=True):
    s = serial.Serial(port(), 115200, timeout=0.2)
    if drain:                       # добрать хвост прошлого кадра, если он был
        end = time.time() + 3.0
        while time.time() < end:
            if s.read(8192):
                end = time.time() + 0.5     # пока льётся — ждём тишины
            else:
                break
        s.reset_input_buffer()
    if reset:
        s.setDTR(False); s.setRTS(False); time.sleep(0.1)
        s.setRTS(True); time.sleep(0.15); s.setRTS(False)
    return s


def clean(b):
    return re.sub(r'\x1b\[[0-9;]*m', '', b.decode('utf-8', 'replace'))


def read_until(s, marker, deadline, tail=b''):
    buf = tail
    while time.time() < deadline:
        buf += s.read(4096)
        i = buf.find(marker)
        if i >= 0:
            return buf[:i], buf[i + len(marker):]
    return None, buf


def shot(s, path, timeout=12):
    s.write(b'p'); s.flush()
    dl = time.time() + timeout
    pre, rest = read_until(s, b'<SHOT %d %d>' % (W, H), dl)
    if pre is None:
        sys.exit('кадр не пришёл; консоль сказала:\n' + clean(rest)[-500:])
    rest = rest.lstrip(b'\r\n')
    need = W * H * 2
    while len(rest) < need and time.time() < dl:
        rest += s.read(4096)
    if len(rest) < need:
        sys.exit(f'пришло {len(rest)} из {need} байт кадра')
    raw, rest = rest[:need], rest[need:]
    from PIL import Image
    im = Image.new('RGB', (W, H))
    px = im.load()
    for y in range(H):
        row = raw[y * W * 2:(y + 1) * W * 2]
        for x in range(W):
            v = (row[2 * x] << 8) | row[2 * x + 1]      # старший байт вперёд
            px[x, y] = (((v >> 11) & 31) << 3, ((v >> 5) & 63) << 2, (v & 31) << 3)
    im.save(path)
    print('кадр ->', path)
    txt = clean(rest)
    if txt.strip().replace('</SHOT>', '').strip():
        print(txt.strip()[:300])


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    what = sys.argv[1]
    if what == 'reset':
        open_port(reset=True).close()
        print('сброшен')
        return
    if what == 'log':
        secs = float(sys.argv[2]) if len(sys.argv) > 2 else 5
        s = open_port()
        end = time.time() + secs
        buf = b''
        while time.time() < end:
            buf += s.read(512)
        print(clean(buf))
        return
    if what == 'shot':
        s = open_port()
        shot(s, sys.argv[2] if len(sys.argv) > 2 else 'build/shot.png')
        return
    if what == 'cmd':
        s = open_port()
        for a in sys.argv[2:]:
            if not (len(a) == 1 and a.isdigit()):     # одиночная цифра — команда, не пауза
                try:
                    time.sleep(float(a))
                    continue
                except ValueError:
                    pass
            if a.endswith('.png'):
                shot(s, a)
                continue
            for ch in a:
                s.write(ch.encode()); s.flush()
                time.sleep(0.08)
        time.sleep(0.3)
        rest = s.read(4096)
        if rest.strip():
            print(clean(rest))
        return
    sys.exit(__doc__)


main()
