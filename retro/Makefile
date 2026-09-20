# EasyAmp retro - Windows 98 SE / XP client.
#   make shots   render every UI state to build/shots/*.png on this machine
#   make win     cross-compile build/EASYAMP.EXE (one exe for 98 SE through XP)
CC      ?= gcc
WINCC   ?= i686-w64-mingw32-gcc
CFLAGS  := -std=c99 -O2 -Wall -Wextra
# no SSE: the floor is a Pentium II / Celeron. The subsystem and OS version
# stamps are what let a real Windows 98 loader accept the file at all.
WINFLAGS := $(CFLAGS) -march=pentium2 -mtune=pentium3 -mno-sse -mfpmath=387 \
            -Wl,--major-subsystem-version,4 -Wl,--minor-subsystem-version,0 \
            -Wl,--major-os-version,4 -Wl,--minor-os-version,0
CORE    := src/gfx.c src/app.c src/ui.c src/fonts_gen.c src/palette_gen.c

build/easyamp-shot: $(CORE) src/main_shot.c src/*.h
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $(CORE) src/main_shot.c -lm

shots: build/easyamp-shot
	@mkdir -p build/shots
	./build/easyamp-shot build/shots
	@python3 -c "import glob; from PIL import Image; [Image.open(f).save(f[:-4]+'.png') for f in glob.glob('build/shots/*.bmp')]"
	@rm -f build/shots/*.bmp

# re-tune the 256-colour palette to the current renders (commit the result)
palette: shots
	python3 tools/mkpalette.py

src/fonts_gen.c: tools/mkfont.py
	python3 tools/mkfont.py

clean:
	rm -rf build

.PHONY: shots clean palette

WINSRC := $(CORE) src/dsp.c src/engine_win32.c src/main_win32.c

build/easyamp_res.o: src/easyamp.rc src/easyamp.ico
	@mkdir -p build
	i686-w64-mingw32-windres -I src src/easyamp.rc -o $@

build/EASYAMP.EXE: $(WINSRC) src/*.h build/easyamp_res.o
	$(WINCC) $(WINFLAGS) -D_WIN32_WINNT=0x0400 -DWINVER=0x0400 -mwindows -static -s -o $@ \
	    $(WINSRC) build/easyamp_res.o -lwinmm -lcomdlg32 -lgdi32 -lshell32

win: build/EASYAMP.EXE

test: build/easyamp-shot
	$(CC) $(CFLAGS) -o build/test_dsp tests/test_dsp.c src/dsp.c src/app.c -lm && ./build/test_dsp

.PHONY: win test
