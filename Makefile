SRC = Musashi/m68kcpu.c Musashi/softfloat/softfloat.c Musashi/m68kops.c
SRC +=  main.c uart.c csr.c ramrom.c

DEPFLAGS = -MT $@ -MMD -MP
CFLAGS=-ggdb -Og -Wall $(DEPFLAGS)

default: emu

emu: $(SRC:.c=.o)
	$(CC) $(CFLAGS) -o $@  $^ -lm

clean:
	rm -f $(SRC:.c=.o) 
	rm -f emu

-include $(SRC_ALL:.c=.d)
