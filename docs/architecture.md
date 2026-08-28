# How ZealMod works inside

[Русская версия](ru/architecture.md)

The short version: the stock firmware stays where it is, we append our own code
to its image and replace three four-byte instructions — and get our own task,
our own screen and our own buttons. Everything else follows from what this
watch does not have.

## The hardware and the stock firmware

The Zeal Smart Timer is an **ESP32-C3** (RISC-V, 160 MHz) with 4 MB of flash, a
240×240 ST7789 over SPI, four buttons, ESP-IDF 4.4 and LVGL 8. The `.gbl` file
is not a Silicon Labs image but an ordinary ESP-IDF application image: a
header, segments, a checksum byte and a SHA-256 at the end.

Flash layout:

```
0x009000 phy_init     0x00a000 nvs        0x00e000 otadata
0x010000 myfat 960K   0x100000 ota_0 2M   0x300000 ota_1 1M
```

`ota_0` holds the timer itself, `ota_1` a separate Bluetooth-update app.
ZealMod fits entirely inside `ota_0`: we simply rewrite that partition with the
composed image. `otadata` is left alone.

## Three jumps

The mod hooks into somebody else's code in exactly three places. Four bytes of
the firmware are replaced with a `j` to our trampoline, and the trampoline
replays the overwritten instructions and jumps back:

| address | what it is | why |
| --- | --- | --- |
| `0x42004C76` | prologue of `TDisplay_task` | once per boot: create our task |
| `0x42005D96` | the LVGL flush callback | take the screen while the mod is open |
| `0x42005426` | the button event dispatch | do not hand our long presses to the firmware |

The addresses and the expected bytes live in
`studio/zealmod/profiles/zeal-v1.json` together with the SHA-256 of the stock
firmware. Studio checks it before composing: a different version means
different addresses, and stopping is the right move.

## Where to find room for code

The least obvious part. On the ESP32-C3 the MMU table is **shared between the
instruction bus and the data bus**: the entry index is `(addr & 0x7FFFFF) >> 16`
no matter whether code (`0x42……`) or data (`0x3C……`) is read through it. The
stock firmware uses entries 0…6 for code and 7…25 for data, so code can only
grow up to the first data page: **47 KB**, and that is it.

So the mod's code lives in two places:

* **the trampolines** (204 bytes) go into the tail of the stock IROM — only
  they have to be there, because a four-byte `j` reaches no further than a
  megabyte;
* **all the rest of the code** sits physically inside the data segment, on a
  flash page boundary. On the first call the trampoline hands the same flash
  page to a free MMU entry (number 40) — this time as code. One entry = 64 KB.

Modules need more than that, so Studio adds as many pages as required and
writes them into `zm_tab.map`; `zg_boot()` maps them before anything can call
into them. Virtually the pages are consecutive from `0x42280000`; physically
they land wherever there is room.

## Where to find room for data

By the time our code starts, the firmware's heap has 8–16 KB left — not enough
for a game. But LVGL owns a full-screen buffer of **115 200 bytes**, and it can
be taken: find the `lv_disp_t`, pause its redraw timer, and LVGL stops drawing
— the buffer is ours until reboot. Silencing LVGL by never calling
`lv_disp_flush_ready` does not work: the firmware's main task then spins and
the watchdog kills the system.

A module's static variables live in RTC memory (about 7 KB); everything else
comes from `mem_alloc()` out of a shared pool. There is no free at all: leaving
the mod is a reboot, and that is the free.

## What survives a power cut

RTC memory holds as long as there is power. So the clock, the high scores and
the settings go to flash — into the last sector of `ota_1` (you may not write
into your own partition; ESP-IDF calls that dangerous and aborts).

Flash may only be written **at the very start of boot**, from the
`TDisplay_task` hook: later the firmware has SPI up, its driver reaches into
flash from an interrupt, and the cache is off during a write — everything
falls over.

## The zm_tab table

The core is built once and knows nothing about the list of programs or the
theme. All of that lives in a `zm_tab` structure in its own `.zmtab` section,
and Studio rewrites it directly inside the finished image:

```
magic 'ZMOD' | MMU pages | version, ABI, end of .bss | number of modules
settings (buttons, hold times, sound, splash, language)
theme (colours, menu style, wallpaper, logo)
modules[32] (address of run(), caption, cover, palette, exit button)
```

The layout is described twice — in `work/src/zmtab.h` and
`studio/zealmod/tab.py` — and both sides assert their sizes so they cannot
drift apart.

One trap cost a debugging session on real hardware: while the table was defined
in the same file that reads it, the compiler saw the zero initialiser and
happily folded every check into "no modules". That is why `zm_tab` is defined
on its own, in `zmtab_data.c`, and nothing else lives there.

## The module linker

A `.zm` carries a relocatable object file (`ld -r`); the addresses are assigned
by Studio, with its own linker in Python (`studio/zealmod/link.py`,
`reloc.py`). It handles exactly what the compiler emits with the SDK flags:
`R_RISCV_CALL_PLT`, `HI20`, `LO12_I`, `LO12_S`, `32`, and a dozen more just in
case.

The point is that **the user's machine needs no compiler**: programs arrive as
machine code, and linking against the core is a matter of substituting
addresses from the core's symbol table. The compatibility check comes for free:
if a module asks for a name the core does not export, you see it before
flashing.

That the linker gets it right is checked automatically:

```sh
python3 studio/tests/test_link.py
```

Every module is laid out twice — by us and by the real GNU ld at the same
addresses — and the results are compared byte for byte.

## Composing the image

`studio/zealmod/build.py` puts it all together:

```
IROM  [ stock code ][ trampolines ]
DROM  [ stock data ][ code page 0: core + modules ]
      [ core data + module data ][ more code pages ]
```

Then come the rules the bootloader enforces: every segment length is a multiple
of four, and for cache-mapped segments the file offset must be congruent to the
virtual address modulo 64 KB (which is why growth of the data segment is padded
to a multiple of 64 KB). Finally the checksum byte and the SHA-256 are
recomputed.

## Building everything from scratch

```sh
cd work && make core          # the core (without -flto: the linker needs real symbol names)
python3 ../studio/zealmod.py mods     # modules, themes and the kit in dist/
python3 ../studio/zealmod.py build --all -o build/zealmod.gbl
python3 ../studio/zealmod.py flash build/zealmod.gbl
```

`make` in `work/` still builds the "everything inside the core" image — the
developer build, handy for iterating on the games themselves.

## Debugging tools

* `work/tools/timer.py` — a remote over USB: open the menu, press a button,
  **grab a frame straight from the watch** (`cmd m 1.5 shot.png`), read the log.
* `zealmod emu` — the same sources under SDL: the menu, the games and the
  splash on your computer, with scripted input and screenshots for checks.
* `work/tools/verify.py` — validates a composed image the way the bootloader
  does.
