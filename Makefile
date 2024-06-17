SRC = Musashi/m68kcpu.c Musashi/softfloat/softfloat.c Musashi/m68kops.c
SRC +=  main.c uart.c csr.c ramrom.c mapper.c scsi.c mbus.c rtc.c

DEPFLAGS = -MT $@ -MMD -MP
CFLAGS=-ggdb -Og -Wall $(DEPFLAGS)

default: emu

Musashi/m68kcpu.o: Musashi/m68kops.h

Musashi/m68kops.h:
	make -C Musashi

emu: $(SRC:.c=.o)
	$(CC) $(CFLAGS) -o $@  $^ -lm

clean:
	rm -f $(SRC:.c=.o) 
	rm -f emu
	rm -f Musashi/m68kops.h

-include $(SRC_ALL:.c=.d)
