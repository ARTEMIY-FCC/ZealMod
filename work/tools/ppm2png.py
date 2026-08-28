#!/usr/bin/env python3
import glob, os
from PIL import Image
for p in sorted(glob.glob('build/shot*.ppm')):
    Image.open(p).resize((480, 480), Image.NEAREST).save(p[:-4] + '.png')
    os.remove(p)
    print(p[:-4] + '.png')
