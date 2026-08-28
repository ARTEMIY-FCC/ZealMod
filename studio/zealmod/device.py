"""Часы по USB: найти, прошить, снять резервную копию, вернуть заводскую.

Заливка идёт через esptool (он же ставится с pip).  Ни кнопок, ни перемычек
нажимать не нужно: у ESP32-C3 разъём разведён на нативный USB, так что часы
сами показываются как «USB JTAG/serial debug unit».
"""
import glob
import os
import subprocess
import sys

OTA0 = 0x100000
OTA0_SIZE = 2 * 1024 * 1024
OTADATA = 0x00E000
FLASH_SIZE = 4 * 1024 * 1024
VID_PID = ('303a', '1001')


class DeviceError(Exception):
    pass


def ports():
    """Похожие на часы последовательные порты, самый вероятный — первым."""
    out = []
    try:
        from serial.tools import list_ports
        for p in list_ports.comports():
            vid = f'{p.vid:04x}' if p.vid is not None else ''
            pid = f'{p.pid:04x}' if p.pid is not None else ''
            score = 0
            if (vid, pid) == VID_PID:
                score = 2
            elif 'JTAG' in (p.description or '') or 'USB Single' in (p.description or ''):
                score = 1
            out.append(dict(port=p.device, desc=p.description or '', vid=vid, pid=pid,
                            serial=p.serial_number or '', score=score))
    except ImportError:
        pats = ('/dev/cu.usbmodem*', '/dev/cu.usbserial*', '/dev/ttyACM*', '/dev/ttyUSB*')
        for pat in pats:
            for p in sorted(glob.glob(pat)):
                out.append(dict(port=p, desc='', vid='', pid='', serial='', score=0))
    out.sort(key=lambda d: (-d['score'], d['port']))
    return out


def esptool_ok():
    try:
        import esptool                      # noqa: F401
        return True
    except ImportError:
        return False


def _esptool(args, port=None, baud=921600, on_line=None):
    cmd = [sys.executable, '-m', 'esptool', '--chip', 'esp32c3']
    if port:
        cmd += ['--port', port]
    cmd += ['--baud', str(baud)] + list(args)
    if on_line is None:
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode:
            raise DeviceError((r.stderr or r.stdout).strip()[-800:])
        return r.stdout
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                         text=True, bufsize=1)
    buf = []
    for line in p.stdout:
        buf.append(line)
        on_line(line.rstrip())
    p.wait()
    if p.returncode:
        raise DeviceError(''.join(buf[-12:]).strip())
    return ''.join(buf)


def info(port=None):
    """Опросить часы: чип, MAC, размер флеша."""
    if not esptool_ok():
        raise DeviceError('нет esptool: pip install esptool')
    txt = _esptool(['flash_id'], port)
    out = {}
    for line in txt.splitlines():
        if ':' not in line:
            continue
        k, v = line.split(':', 1)
        k, v = k.strip().lower(), v.strip()
        if k in ('chip is', 'features', 'crystal is', 'mac', 'detected flash size',
                 'manufacturer', 'device'):
            out[k] = v
    return out


def flash(image_path, port=None, on_line=None, set_boot=False):
    """Записать образ в раздел ota_0."""
    if not esptool_ok():
        raise DeviceError('нет esptool: pip install esptool')
    size = os.path.getsize(image_path)
    if size > OTA0_SIZE:
        raise DeviceError(f'образ {size} Б больше раздела ota_0 ({OTA0_SIZE} Б)')
    args = ['write_flash', hex(OTA0), str(image_path)]
    if set_boot:
        import tempfile
        p = os.path.join(tempfile.gettempdir(), 'zealmod-otadata.bin')
        open(p, 'wb').write(otadata(1) + b'\xff' * 0x1000)
        args += [hex(OTADATA), p]
    return _esptool(args, port, on_line=on_line)


def backup(out_path, port=None, on_line=None):
    """Полный дамп флеша — прежде чем что-то менять."""
    if not esptool_ok():
        raise DeviceError('нет esptool: pip install esptool')
    return _esptool(['read_flash', '0', hex(FLASH_SIZE), str(out_path)], port,
                    on_line=on_line)


def restore(path, port=None, on_line=None):
    return _esptool(['write_flash', '0', str(path)], port, on_line=on_line)


def otadata(seq):
    """Слот otadata: номер и crc32 по нему (полином из ESP-IDF)."""
    import struct

    def crc32(data):
        crc = 0xFFFFFFFF
        for b in data:
            crc ^= b
            for _ in range(8):
                crc = (crc >> 1) ^ (0xEDB88320 if crc & 1 else 0)
        return (~crc) & 0xFFFFFFFF

    blob = struct.pack('<I', seq) + b'\xff' * 24
    blob += struct.pack('<I', crc32(struct.pack('<I', seq)))
    return blob + b'\xff' * (0x1000 - len(blob))
