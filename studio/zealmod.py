#!/usr/bin/env python3
"""Запуск ZealMod без установки: python3 studio/zealmod.py <команда>"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from zealmod.cli import main

sys.exit(main())
