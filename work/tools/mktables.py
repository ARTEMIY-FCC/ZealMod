#!/usr/bin/env python3
"""Тригонометрия целыми: build/tables.c"""
import math, os
HERE = os.path.dirname(os.path.abspath(__file__))
out = os.path.join(HERE, '..', 'build', 'tables.c')
N = 1024              # полный оборот = 1024 «градуса»
Q = 1 << 14           # 1.0 = 16384
sin_q = [round(math.sin(2 * math.pi * i / N) * Q) for i in range(N // 4 + 1)]
# atan(t) для t = 0..1 (256 шагов) -> угол в 1/1024 оборота
atan_t = [round(math.atan(i / 256) / (2 * math.pi) * N) for i in range(257)]
with open(out, 'w') as f:
    f.write('/* сгенерировано tools/mktables.py */\n#include <stdint.h>\n\n')
    f.write('const int16_t g_sin_q14[%d] = {' % len(sin_q))
    for i, v in enumerate(sin_q):
        if i % 16 == 0: f.write('\n    ')
        f.write('%d,' % v)
    f.write('\n};\n\nconst uint16_t g_atan_q[257] = {')
    for i, v in enumerate(atan_t):
        if i % 16 == 0: f.write('\n    ')
        f.write('%d,' % v)
    f.write('\n};\n')
print('tables ->', out, len(sin_q), 'sin,', len(atan_t), 'atan')
