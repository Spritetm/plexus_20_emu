SRC = Musashi/m68kcpu.c Musashi/softfloat/softfloat.c Musashi/m68kops.c
SRC += main.c uart.c csr.c ramrom.c mapper.c scsi.c mbus.c rtc.c log.c 
SRC += emu.c scsi_dev_hd.c rtcram.c

DEPFLAGS = -MT $@ -MMD -MP
CFLAGS=-ggdb -Og -Wall $(DEPFLAGS)

default: emu

Musashi/m68kcpu.o: Musashi/m68kops.h

Musashi/m68kops.h:
	make -C Musashi

emu: $(SRC:.c=.o)
	$(CC) $(CFLAGS) -o $@  $^ -lm


# Note that PROXY_TO_PTHREAD doesn't generally work as the needed
# SharedArrayBuffer needs some pretty specific server settings.
EMCC_ARGS = -s ASYNCIFY
#EMCC_ARGS = -s PROXY_TO_PTHREAD -pthread
EMCC_ARGS += --js-library xterm-pty/emscripten-pty.js
EMCC_ARGS += --preload-file plexus-sanitized.img
EMCC_ARGS += --preload-file U15-MERGED.BIN
EMCC_ARGS += --preload-file U17-MERGED.BIN
EMCC_ARGS += -O2 -gsource-map --source-map-base=./

plexem.mjs: $(SRC)
	emcc -o $@ $(EMCC_ARGS) $^ -lm 

clean:
	rm -f $(SRC:.c=.o) 
	rm -f emu
	rm -f Musashi/m68kops.h

-include $(SRC:.c=.d)
