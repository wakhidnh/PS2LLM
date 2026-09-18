# ============================================================================
# PS2LLM - Local LLM on PlayStation 2 (Makefile)
# ============================================================================
# 
# Copyright (c) 2026 Wakhid Nurhidayat (@wakhidnh)
# 
# License Terms:
# - Free to use for all kinds of devices (Homebrew, Consoles, PCs, Embedded, etc.)
# - Non-Commercial Purpose Only: This software and its source may NOT be used 
#   for any commercial purposes, profit-driven products, or paid distribution.
# - Attribution: You must include this original source notice and credit 
#   Wakhid Nurhidayat (@wakhidnh) in your project source or documentation.
# 
# Support the creator: If you like this project, feel free to buy a coffee 
# on Ko-fi: ko-fi.com/wakhidnh (or @wakhidnh)
# ============================================================================

EE_BIN = PS2LLM.ELF
EE_OBJS = main.o model.o keyboard.o bpe.o storage.o config.o irx_loader.o gs_ui.o

EE_LIBS = -ldebug -lpad -lpatches -lfileXio -ldraw -lgraph -ldma -lc -lkernel

# Mapped to EE_CFLAGS and EE_LDFLAGS so PS2SDK applies them during build
EE_CFLAGS = -D_EE -G0 -O3 -flto -march=r5900 -mtune=r5900 -funroll-loops -falign-functions=64 -falign-loops=64 -fomit-frame-pointer -Wall -Isrc
EE_LDFLAGS = -O3 -flto -s

VPATH = src

all: $(EE_BIN)
	$(EE_STRIP) --strip-all $(EE_BIN)

$(EE_BIN): $(EE_OBJS)

keyboard.o: irx_data.h irx_loader.h
storage.o: irx_data.h irx_loader.h config.h storage_internal.h storage.h
irx_loader.o: irx_data.h irx_loader.h
bpe.o: vocab.h bpe.h
model.o: vocab.h model.h
main.o: model.h bpe.h keyboard.h storage.h vocab.h font.h
gs_ui.o: font.h
config.o: config.h storage_internal.h

clean:
	rm -f $(EE_OBJS) $(EE_BIN)

run: $(EE_BIN)
	ps2client execee host:$(EE_BIN)

reset:
	ps2client reset

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal