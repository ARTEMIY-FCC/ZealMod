# Writing a program for ZealMod

[Русская версия](ru/module.md)

A program for the timer is a `.zm`: a zip archive with ready machine code, a
cover and a manifest. Whoever installs it needs no compiler — they just drag
the file onto the Studio window. The compiler is only needed by you, the
author.

```sh
python3 studio/zealmod.py new app mygame   # scaffold
python3 studio/zealmod.py emu mygame       # run it on the computer
python3 studio/zealmod.py pack mygame      # build mygame.zm
python3 studio/zealmod.py check mygame.zm  # check compatibility
```

You need `riscv64-elf-gcc` (macOS: `brew install riscv64-elf-gcc`) and SDL2 for
the emulator.

The full list of calls is in [api.md](api.md).

## What the scaffold gives you

```
mygame/
  module.json    manifest: name, exit button, list of sources
  cover.png      96×96 cover — what the menu shows
  src/main.c     the program itself
```

`module.json`:

```json
{
  "id": "mygame",              identifier: high scores are keyed by it
  "name": "My game",           menu caption
  "name_ru": "Моя игра",       caption when the watch is set to Russian
  "version": "1.0",
  "author": "me",
  "kind": "game",              game or app — only affects the Studio list
  "entry": "zm_main",          the function it all starts from
  "exit_button": "up",         which button leaves it
  "exit_hold": 1.4,            how many seconds to hold it
  "cover": "cover.png",
  "sources": ["src/main.c"]
}
```

## What a program looks like

```c
#include "plat.h"
#include "game.h"

void zm_main(void)
{
    game_exit_button(BTN_UP);
    uint32_t last = now_ms();
    while (!game_quit()) {
        in_poll();
        uint32_t hit = in_rep();
        ...
        fb_begin();
        for (band *b; (b = fb_next()); ) {
            gfx_clear(b, RGB(12, 14, 22));
            gfx_text_c(b, 120, 120, &font_m, WHITE, TR("hello", "привет"));
        }
        game_frame_wait(&last, 33);      /* ~30 frames per second */
    }
}
```

Three rules make watch code different from ordinary code:

1. **A frame is drawn in bands.** `fb_begin()` starts a frame, `fb_next()`
   hands out the next band. On today's watch there is a single band covering
   the screen, but never rely on it: draw the whole scene inside the loop.
2. **Every frame must end with `game_frame_wait()`.** It paces the frame and
   gives time back to the system; skip it and the watchdog reboots the watch.
3. **Returning.** `game_quit()` becomes true when the user has held the exit
   button — just return from `zm_main()`.

## Two languages

The image carries both languages and picks one at runtime, so texts go through
`TR()`:

```c
gfx_text_c(b, 120, 40, &font_m, WHITE, TR("Score", "Счёт"));
```

For tables of labels there is `TRA()`, which picks one of two arrays. If you
only care about one language, write the same string twice — nothing breaks.

## Limits worth knowing up front

* **No initialised mutable data.** `static int x = 5;` will not work: there is
  no one to copy a `.data` section into RAM, and the packer rejects such a
  module. Write `static int x;` and assign at the start of `zm_main()`, or make
  it `static const`.
* **No 64-bit division** — libgcc is not linked. Multiplication is fine.
* **No `malloc`.** There is `mem_alloc()`: memory comes from a shared pool
  (about 16 KB) and is reclaimed when the program ends.
* **No `printf`.** There is `fx_fmt(buf, sizeof buf, "%d", v)`.
* **`.bss` is scarce.** A module's static variables live in RTC memory, about
  seven kilobytes for all programs together. Big arrays belong in `mem_alloc()`.
* **The stack is small** (12 KB for everything), so deep recursion is risky.

All of this is checked at `pack` and `check` time: if something is wrong you
get words, not silence on the watch.

## Your own data

If the program needs images, levels or tables, keep them as files and declare
them in `module.json`:

```json
"blobs": { "my_level_data": "data/level1.bin" }
```

In the code it is a plain array:

```c
extern const uint8_t my_level_data[];
```

Studio puts the file into the image and substitutes the address at install
time. That is how DOOM works: 400 KB of maps and textures ride along as a
separate chunk.

## What happens at install time

A `.zm` carries a **relocatable object file** — machine code without addresses.
Studio lays its sections out over the free space of the image, substitutes the
addresses of the core functions and patches the instructions (that is the
linker, `studio/zealmod/link.py`). This is why the same program fits into any
selection in any order, and why the compatibility check is literally "does the
core export every name this module asks for".

If the core ever changes and drops something from the ABI, `zealmod check`
tells you before flashing rather than after.
