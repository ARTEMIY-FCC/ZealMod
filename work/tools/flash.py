#!/usr/bin/env python3
"""Заливка по USB.

  python3 tools/flash.py build/zeal-mod.gbl          — записать в ota_0 и указать на него
  python3 tools/flash.py --backup                     — снять полный дамп флеша
  python3 tools/flash.py --restore ~/Downloads/zeal-full-flash.bin
  python3 tools/flash.py --stock                      — вернуть заводской ota_0

Разделы (из таблицы разделов прошивки):
  otadata 0x00e000  8K      myfat 0x010000  960K
  ota_0   0x100000  2M      ota_1 0x300000  1M   (BLE_Update)
"""
import subprocess, sys, os, struct, glob

OTA0, OTA1, OTADATA = 0x100000, 0x300000, 0x00E000
HERE = os.path.dirname(os.path.abspath(__file__))
STOCK = os.path.join(HERE, '..', '..', 'original.gbl')


def ports():
    p = sorted(glob.glob('/dev/cu.usbmodem*') + glob.glob('/dev/cu.usbserial*') +
               glob.glob('/dev/cu.SLAB*') + glob.glob('/dev/cu.wchusbserial*'))
    return p


def esptool(*args):
    port = ports()
    cmd = [sys.executable, '-m', 'esptool', '--chip', 'esp32c3']
    if port:
        cmd += ['--port', port[0]]
    cmd += ['--baud', '921600'] + list(args)
    print('$', ' '.join(cmd))
    return subprocess.call(cmd)


def otadata(seq):
    """Один слот otadata: seq + crc32 по seq (полином ESP-IDF)."""
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


def main():
    if not ports():
        print('!! не вижу USB-порта таймера. Подключи и проверь: ls /dev/cu.*')
    a = sys.argv[1:]
    if a and a[0] == '--backup':
        return esptool('read_flash', '0', '0x400000', 'build/zeal-full-backup.bin')
    if a and a[0] == '--restore':
        return esptool('write_flash', '0', a[1])
    img = STOCK if (a and a[0] == '--stock') else (a[0] if a else 'build/zeal-mod.gbl')
    size = os.path.getsize(img)
    if size > 2 * 1024 * 1024:
        sys.exit('образ больше раздела ota_0')
    print(f'{img}: {size} байт -> ota_0 @ {OTA0:#x}')
    args = ['write_flash', hex(OTA0), img]
    if '--set-boot' in a:      # только если otadata вдруг указывает на ota_1
        open('build/otadata.bin', 'wb').write(otadata(1) + b'\xff' * 0x1000)
        args += [hex(OTADATA), 'build/otadata.bin']
    rc = esptool(*args)
    if rc == 0:
        print('готово. Питание передёрнуть не нужно — esptool сам перезагрузит.')
    return rc


sys.exit(main())
