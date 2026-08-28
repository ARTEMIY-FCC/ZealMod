/* сгенерировано tools/gen_ld.py */
ENTRY(zg_hook_boot)
MEMORY {
  irom  (rx) : ORIGIN = 0x420647d0, LENGTH = 47152
  irom2 (rx) : ORIGIN = 0x42280000, LENGTH = 65536
  drom  (r)  : ORIGIN = 0x3c120000, LENGTH = 768K
  rtc   (rw) : ORIGIN = 0x50000140, LENGTH = 7616
}
SECTIONS {
  /* Трамплины: до них хук достаёт четырёхбайтным `j`, значит они обязаны
     лежать в хвосте стоковой IROM.  Больше сюда ничего не кладём — места
     до первой страницы данных всего 47152 байт. */
  .ptext : ALIGN(4) {
    *(.text.hooks)
    . = ALIGN(4);
  } > irom
  /* Весь остальной код: лежит в сегменте данных, а работает по этому адресу —
     страницу отображает трамплин перед первым же вызовом. */
  .ptext2 : ALIGN(4) {
    *(.text .text.*)
    . = ALIGN(4);
  } > irom2
  .prodata : ALIGN(4) {
    *(.zmtab)                 /* таблица ZealMod: её переписывает Studio */
    *(.rodata .rodata.* .srodata .srodata.*)
    . = ALIGN(4);
  } > drom
  .pbss (NOLOAD) : ALIGN(8) {
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
  } > rtc
  /DISCARD/ : { *(.comment) *(.eh_frame*) *(.riscv.attributes) }
}
ASSERT(SIZEOF(.pbss) < 7616, "RTC-память кончилась")
