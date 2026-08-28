# Zeal Smart Timer — мод с играми.
#   make            собрать прошивку build/zeal-mod.gbl
#   make host       десктопный стенд build/zgames
#   make shots      прогнать сценарий и сохранить кадры
#   make flash      залить в ota_0 по USB (нужен esptool)

SRC := src/gfx.c src/text.c src/util.c src/menu.c src/apps.c src/gameutil.c \
       src/zmtab.c src/zmtab_data.c src/splash.c \
       $(wildcard src/games/*.c) $(wildcard src/games/doom/*.c) $(wildcard src/apps/*.c) \
       build/fonts.c build/tables.c build/covers.c build/doom_music.c build/doom_data.S
DEV := $(SRC) src/dev/dev_esp32.c src/dev/snd_dev.c src/dev/nvram.c
HOST := $(SRC) host/host_sdl.c host/snd_host.c host/nvram_host.c

CC_DEV    := riscv64-elf-gcc
DEVFLAGS  := -march=rv32imc_zicsr -mabi=ilp32 -mno-relax -msmall-data-limit=0 -Os -flto \
             -ffreestanding -nostdlib -fno-common -fno-strict-aliasing \
             -Wall -Wextra -Wno-unused-parameter -Isrc -Ibuild -Isrc/games/doom
HOSTFLAGS := -DPLAT_HOST=1 -O2 -g -Wall -Wextra -Wno-unused-parameter -Isrc -Ibuild -Isrc/games/doom \
             $(shell sdl2-config --cflags)
HOSTLIBS  := $(shell sdl2-config --libs)

GEN := build/fonts.c build/tables.c build/covers.c build/doom_music.c build/payload.ld build/layout.h

all: build/zeal-mod.gbl

build/fonts.c:  tools/mkfont.py   ; python3 $<
build/tables.c: tools/mktables.py ; python3 $<
build/covers.c: tools/mkcovers.py ; python3 $<
build/doom_music.c: ; @test -f $@ || python3 tools/mkdoom.py --wad ~/Desktop/.zepgam-wad/freedoom-0.13.0/freedoom1.wad --maps E1M1
build/payload.ld build/layout.h: tools/gen_ld.py tools/img.py ; python3 $<

build/payload.elf: $(GEN) $(DEV) $(wildcard src/*.h)
	$(CC_DEV) $(DEVFLAGS) -T build/payload.ld -Wl,--build-id=none -Wl,-Map=build/payload.map \
	    -o $@ $(DEV)
	@riscv64-elf-size $@

build/zeal-mod.gbl: build/payload.elf tools/patch.py
	python3 tools/patch.py $< $@

# --- ZealMod: ядро отдельно, приложения — модулями ------------------------
# Ядро собирается без -flto: имена функций должны остаться на месте, по ним
# Studio связывает модули (см. ../studio/zealmod/link.py).
CORE := src/gfx.c src/text.c src/util.c src/menu.c src/apps.c src/gameutil.c \
        src/zmtab.c src/zmtab_data.c src/splash.c src/apps/wallclock.c build/fonts.c build/tables.c \
        src/dev/dev_esp32.c src/dev/snd_dev.c src/dev/nvram.c
COREFLAGS := $(filter-out -flto,$(DEVFLAGS)) -DZM_NO_BUILTINS

build/core.elf: $(GEN) $(CORE) $(wildcard src/*.h)
	$(CC_DEV) $(COREFLAGS) -T build/payload.ld -Wl,--build-id=none -Wl,-Map=build/core.map \
	    -o $@ $(CORE)
	@riscv64-elf-size $@

core: build/core.elf

# Модули: каждый собирается отдельно и пакуется в .zm (см. modules/*/module.json)
mods: build/core.elf
	python3 ../studio/zealmod.py mods

# Готовый образ «всё включено» — той же дорогой, какой его собирает Studio
zealmod: mods
	python3 ../studio/zealmod.py build --all -o build/zealmod.gbl

build/zgames: $(GEN) $(HOST) $(wildcard src/*.h)
	cc $(HOSTFLAGS) -o $@ $(HOST) $(HOSTLIBS)

host: build/zgames

shots: build/zgames
	./build/zgames --script "$(SCRIPT)" --shots $(SHOTS) --out build/shot
	python3 tools/ppm2png.py

flash: build/zeal-mod.gbl
	python3 tools/flash.py build/zeal-mod.gbl

clean: ; rm -rf build/*.o build/*.elf build/*.gbl build/*.bin build/shot*.p* 

.PHONY: all host shots flash clean core mods zealmod
