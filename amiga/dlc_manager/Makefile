# SWOS United DLC Manager for the Amiga -- Bartman's amiga-debug toolchain
# (m68k-amiga-elf-gcc + elf2hunk), the same one SWOS-United builds with.
#
#   make                      build out/SWOS-DLC (an AmigaDOS executable)
#   make CPU=68020            target a 68020+ instead of a plain 68000
#
# With the toolchain's bin/win32 and bin/win32/opt/bin on PATH this runs as
# is on Windows (the extension's gnumake) or Linux/macOS. From WSL, point at
# the Windows binaries, e.g.:
#   make TOOLCHAIN=/mnt/c/Users/<you>/.vscode/extensions/bartmanabyss.amiga-debug-1.8.2/bin/win32
#
# No libc: the startup, string helpers and 68000 multiply/divide are in
# src/.
#
# Networking is built against the Roadshow SDK (ROADSHOW_SDK, below). The
# toolchain bundles an older Roadshow netinclude too, but it is patched to
# expect a C library (ssize_t, stdint.h, sys/errno.h) this build does not
# have; the SDK's own headers are self-contained. Only net.c sees them, since
# the SDK also brings an older <inline/macros.h> that the rest of the NDK
# inlines should not pick up.

# On Windows, recipes run under cmd.exe even when a Cygwin/MSYS sh is on PATH
# (gnumake would otherwise pick that up), as the amiga-debug template does.
ifdef OS
	WINDOWS = 1
	SHELL = cmd.exe
endif

ifdef TOOLCHAIN
	CC       = $(TOOLCHAIN)/opt/bin/m68k-amiga-elf-gcc.exe
	ELF2HUNK = $(TOOLCHAIN)/elf2hunk.exe
else
	CC       = m68k-amiga-elf-gcc
	ELF2HUNK = elf2hunk
endif

CPU      ?= 68000
ROADSHOW_SDK ?= ../../../../../Amiga/Roadshow/Roadshow-SDK-1.8
PROGRAM  = out/SWOS-DLC

# startup.c first: AmigaDOS enters at the first byte of the first hunk.
SOURCES  = src/startup.c src/main.c src/catalog.c src/net.c src/util.c src/fio.c \
           src/json.c src/sha256.c src/inflate.c src/unzip.c src/mods.c
OBJECTS  = $(patsubst src/%.c,obj/%.o,$(SOURCES))
HEADERS  = $(wildcard src/*.h)

CFLAGS   = -Isrc/shim -m$(CPU) -Os -g -nostdlib -ffreestanding -fno-builtin \
           -fomit-frame-pointer -fno-reorder-blocks-and-partition \
           -ffunction-sections -fdata-sections \
           -Wall -Wextra -Wno-unused-parameter -Wno-pointer-sign \
           -Wno-volatile-register-var -Wno-array-bounds
LDFLAGS  = -nostdlib -Wl,--emit-relocs,--gc-sections,-e,_start,-Ttext=0,-Map=$(PROGRAM).map

all: $(PROGRAM) $(PROGRAM).info

# The Workbench icon (made by tools/make_icon.py) goes next to the program.
$(PROGRAM).info: SWOS-DLC.info | out
ifdef WINDOWS
	copy /y $(subst /,\,$<) $(subst /,\,$@) >nul
else
	cp $< $@
endif

$(PROGRAM): $(PROGRAM).elf
	$(ELF2HUNK) $< $@

$(PROGRAM).elf: $(OBJECTS) | out
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJECTS) -o $@

# Roadshow's <proto/bsdsocket.h> only picks its inline calls when __AMIGA__
# is defined (amiga-elf GCC does not define it), and those inlines use MIT
# syntax without the '%' register prefix.
obj/net.o: CFLAGS += -I$(ROADSHOW_SDK)/netinclude -D__AMIGA__ -Wa,--register-prefix-optional

obj/%.o: src/%.c $(HEADERS) | obj
	$(CC) $(CFLAGS) -c $< -o $@

obj out:
	mkdir $@

clean:
ifdef WINDOWS
	-rmdir /s /q obj out
else
	rm -rf obj out
endif

.PHONY: all clean
