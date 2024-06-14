#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "uart.h"
#include "emu.h"

//The UARTs are Mostek MK68564 chips.

typedef struct {
	uint8_t regs[32];
	uint8_t char_rcv;
	uint8_t has_char_rcv;
} chan_t;

struct uart_t {
	char *name;
	int is_console;
	chan_t chan[2];
};

uart_t *uart_new(const char *name, int is_console) {
	uart_t *u=calloc(sizeof(uart_t), 1);
	u->name=strdup(name);
	u->is_console=is_console;
	return u;
}

#define REG_CMD 0
#define REG_MODECTL 1
#define REG_INTCTL 2
#define REG_SYNC1 3
#define REG_SYNC2 4
#define REG_RCVCTL 5
#define REG_XMTCTL 6
#define REG_STAT0 7
#define REG_STAT1 8
#define REG_DATA 9
#define REG_TC 10
#define REG_BRG 11
#define REG_VECT 12

void uart_write8(void *obj, unsigned int addr, unsigned int val) {
	uart_t *u=(uart_t*)obj;
	addr=addr/2; //8-bit thing on 16-bit bus
	int chan=(addr>>4)&1;
	int a=(addr&0xf);
	if (a==0) {
		printf("uart %s chan %s: cmd 0x%X\n", u->name, chan?"B":"A", val);
	} else if (a==1) {
		printf("uart %s chan %s: mode 0x%X\n", u->name, chan?"B":"A", val);
	} else if (a==2) {
		printf("uart %s chan %s: int ctl 0x%X\n", u->name, chan?"B":"A", val);
	} else if (a==5) {
		printf("uart %s chan %s: rcv ctl 0x%X\n", u->name, chan?"B":"A", val);
	} else if (a==6) {
		printf("uart %s chan %s: tx ctl 0x%X\n", u->name, chan?"B":"A", val);
	} else if (a==9) {
//		printf("uart %s chan %s: data reg 0x%X\n", u->name, chan?"B":"A", val);
		if (u->chan[chan].regs[0]&1) { //loop mode
			u->chan[chan].has_char_rcv=1;
			u->chan[chan].char_rcv=val;
			if (u->chan[chan].regs[REG_INTCTL] & 0x18) {
				raise_int(u->chan[chan].regs[REG_VECT]);
			}
			printf("uart %s chan %s: send loopback char 0x%X\n", u->name, chan?"B":"A", val);
			dump_cpu_state();
		} else {
			if (u->is_console) printf("%c", val);
		}
	} else if (a==10) {
		printf("uart %s chan %s: time const reg 0x%X\n", u->name, chan?"B":"A", val);
	} else if (a==11) {
		printf("uart %s chan %s: baud rate gen 0x%X\n", u->name, chan?"B":"A", val);
	} else if (a==12) {
		printf("uart %s chan %s: vector ctl 0x%X\n", u->name, chan?"B":"A", val);
	}
	u->chan[chan].regs[addr]=val;
}

unsigned int uart_read8(void *obj, unsigned int addr) {
	uart_t *u=(uart_t*)obj;
	addr=addr/2; //8-bit thing on 16-bit bus
	int chan=(addr>>4)&1;
	int a=(addr&0xf);
	if (a==7) {
		//D7-0: break, underrun, cts, hunt, dcd, tx buf empty, int pending, rx char avail
		int r=0x4;
		if (u->chan[chan].has_char_rcv) r|=0x3;
//		printf("uart %s chan %s: read8 status0 -> %x\n", u->name, chan?"B":"A", addr, r);
		return r;
	} else if (a==8) {
		//D7-0: eof, crc err, rx overrun, parity err, res c2, res c1, res c0, all sent
		int r=0;
//		printf("uart %s chan %s: read8 status1 -> %x\n", u->name, chan?"B":"A", addr, r);
		return r;
	} else if (a==9) {
		printf("read char %x\n", u->chan[chan].char_rcv);
		u->chan[chan].has_char_rcv=0;
		return u->chan[chan].char_rcv;
	}
	
	printf("uart %s chan %s: read8 %x -> %x\n", u->name, chan?"B":"A", addr, u->chan[chan].regs[addr]);
	return u->chan[chan].regs[addr];
}


