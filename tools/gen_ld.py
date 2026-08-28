#!/usr/bin/env python3
"""Считает, куда лечь нашему коду, и пишет build/payload.ld и build/layout.h.

Раскладку целиком объясняет img.layout(): у ESP32-C3 таблица MMU общая для
команд и данных, поэтому в конец IROM влезает всего 47 КБ, а остальной код
едет вторым куском внутрь сегмента данных и отображается на старте.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from img import Img, layout, MMU_TABLE, MMU_PAGE

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', 'build')

BSS_BASE = 0x50000140          # RTC fast: прошивка занимает только 24 байта в начале
BSS_END = 0x50001F00           # хвост RTC оставлен под заглушку для флеш-операций

L = layout(Img())

os.makedirs(OUT, exist_ok=True)
open(os.path.join(OUT, 'payload.ld'), 'w').write(f"""/* сгенерировано tools/gen_ld.py */
ENTRY(zg_hook_boot)
MEMORY {{
  irom  (rx) : ORIGIN = {L['text']:#x}, LENGTH = {L['text_len']}
  irom2 (rx) : ORIGIN = {L['text2']:#x}, LENGTH = {MMU_PAGE}
  drom  (r)  : ORIGIN = {L['data']:#x}, LENGTH = 768K
  rtc   (rw) : ORIGIN = {BSS_BASE:#x}, LENGTH = {BSS_END - BSS_BASE}
}}
SECTIONS {{
  /* Трамплины: до них хук достаёт четырёхбайтным `j`, значит они обязаны
     лежать в хвосте стоковой IROM.  Больше сюда ничего не кладём — места
     до первой страницы данных всего {L['text_len']} байт. */
  .ptext : ALIGN(4) {{
    *(.text.hooks)
    . = ALIGN(4);
  }} > irom
  /* Весь остальной код: лежит в сегменте данных, а работает по этому адресу —
     страницу отображает трамплин перед первым же вызовом. */
  .ptext2 : ALIGN(4) {{
    *(.text .text.*)
    . = ALIGN(4);
  }} > irom2
  .prodata : ALIGN(4) {{
    *(.zmtab)                 /* таблица ZealMod: её переписывает Studio */
    *(.rodata .rodata.* .srodata .srodata.*)
    . = ALIGN(4);
  }} > drom
  .pbss (NOLOAD) : ALIGN(8) {{
    __bss_start__ = .;
    *(.bss .bss.* .sbss .sbss.* COMMON)
    . = ALIGN(8);
    __bss_end__ = .;
    __persist_magic = .;
    . += 4;
    __persist_start__ = .;
    *(.persist .persist.*)
    . = ALIGN(8);
    __persist_end__ = .;
  }} > rtc
  /DISCARD/ : {{ *(.comment) *(.eh_frame*) *(.riscv.attributes) }}
}}
ASSERT(SIZEOF(.pbss) < {BSS_END - BSS_BASE}, "RTC-память кончилась")
""")

open(os.path.join(OUT, 'layout.h'), 'w').write(f"""/* сгенерировано tools/gen_ld.py */
#ifndef ZG_LAYOUT_H
#define ZG_LAYOUT_H
#define ZG_MMU_SLOT  {MMU_TABLE + L['entry'] * 4:#x}   /* запись MMU под второй кусок кода */
#define ZG_MMU_PAGE  {L['page']}          /* страница флеша, где он лежит */
#define ZG_TEXT2     {L['text2']:#x}
#endif
""")
print(f".ptext   -> {L['text']:#010x} ({L['text_len']} байт до страницы данных)")
print(f".ptext2  -> {L['text2']:#010x} (лежит по {L['pt2_lma']:#x}, страница флеша {L['page']})")
print(f".prodata -> {L['data']:#010x}")
print(f".pbss    -> {BSS_BASE:#010x} ({BSS_END - BSS_BASE} байт)")
