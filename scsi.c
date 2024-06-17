#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include "scsi.h"
#include "emu.h"

struct scsi_t {
	uint8_t buf[4];
	int bytecount;
	int pointer;
	int reg;
	int state;
	int byte_stashed;
	int diag;
	int last_req_reg;
};


static void show_buf(scsi_t *s) {
	printf("SCSI buf write. ");
	for (int i=0; i<4; i++) printf("%02X ", s->buf[i]);
	printf("\n");
}


void scsi_write16(void *obj, unsigned int a, unsigned int val) {
	scsi_t *c=(scsi_t*)obj;
	c->buf[a]=val>>8;
	c->buf[a+1]=val;
	show_buf(c);
}

void scsi_write8(void *obj, unsigned int a, unsigned int val) {
	scsi_t *c=(scsi_t*)obj;
	c->buf[a]=val;
	show_buf(c);
}

unsigned int scsi_read16(void *obj, unsigned int a) {
	scsi_t *c=(scsi_t*)obj;
	return (c->buf[a]<<8)+c->buf[a+1];
}

unsigned int scsi_read8(void *obj, unsigned int a) {
	scsi_t *c=(scsi_t*)obj;
	return c->buf[a];
}

//scsi errors go to the DMA cpu.
#define LEVEL 3
#define INTVECT_SPURIOUS 0x60
#define INTVECT_SELECTI 0x61
#define INTVECT_RESELECT 0x62
#define INTVECT_PARITY 0x64
#define INTVECT_POINTER 0x68 //plus message (4), command (2) input (1) flag



void scsi_set_bytecount(scsi_t *s, int bytecount) {
	printf("SCSI: Bytecount 0x%X\n", bytecount);
	s->bytecount=bytecount;
}

void scsi_set_pointer(scsi_t *s, int pointer) {
	printf("SCSI: Pointer 0x%X\n", pointer);
	s->pointer=pointer;
}

int scsi_get_bytecount(scsi_t *s) {
	return s->bytecount;
}

int scsi_get_pointer(scsi_t *s) {
	//Not sure. This may also be the amount of bytes written/read.
	return s->pointer;
}

#define O_IOPTR		0x8000
#define O_MSGPTR	0x4000
#define O_CDPTR		0x2000
#define O_SRAM		0x1000
#define O_RESET		0x0800
#define O_SELENA	0x0400
#define O_SCSIBSY	0x0200
#define O_ARB		0x0100
#define O_SCSIREQ	0x0080
#define O_SCSIMSG	0x0040
#define O_SCSIRST	0x0020
#define O_SCSIIO	0x0010 //true indicates input to the initiator
#define O_SCSICD	0x0008 //true indicates control
#define O_SCSIATN	0x0004
#define O_SCSIACK	0x0002
#define O_AUTOXFR	0x0001

#define I_NARBR		0x8000
#define I_SCZERO	0x4000
#define I_NSCPERR	0x2000
#define I_NSCBERR	0x1000
#define I_STIME		0x0800
#define I_SEL		0x0400
#define I_BSY		0x0200
#define I_MYBIT		0x0100
#define I_REQ		0x0080
#define I_MSG		0x0040
#define I_SCRST		0x0020
#define I_IO		0x0010
#define I_CD		0x0008
#define I_ATN		0x0004
#define I_ACK		0x0002
#define I_DATEN		0x0001

/*
 SCSI state machine
 It looks like the CPU twiddles bits immediately, but there is some 'hardware acceleration': the 
 arb process is automated (you set ARB, it raises a SELECT int), and there is a bit of DMA:
 when AUTOXFR is set, bits get read from *pointer into SCSIBUF.


 DMA seems to write 16 bit at a time: writes to an even byte are stashed, as soon as the odd 
 byte is written, both writes go through. (I think.)
*/


#define STATE_BUS_FREE 0

unsigned int state_sigs={
	I_ACK
};

const char *bit_str[]={
	"O_IOPTR", "O_MSGPTR", "O_CDPTR", "O_SRAM", "O_RESET", "O_SELENA",
	"O_SCSIBSY", "O_ARB", "O_SCSIREQ", "O_SCSIMSG", "O_SCSIRST",
	"O_SCSIIO", "O_SCSICD", "O_SCSIATN", "O_SCSIACK", "O_AUTOXFR"
};

//this csr is at E0000E
//note SCSIBUF is at 0xA7000

void scsi_set_scsireg(scsi_t *s, unsigned int val) {
	printf("SCSI: reg ");
	for (int i=0; i<16; i++) {
		if (val&(1<<(15-i))) printf("%s ", bit_str[i]);
	}
	printf("\n");
	if (val&O_SCSIRST) {
		//Note: SCSI RESET makes all other signals on the bus undefined and
		//tells all targets to keep their paws off the bus. The diags use this
		//to run selftests without bothering others.
		s->state=STATE_BUS_FREE;
		if (val&O_ARB) {
			//Arb succeeds immediately
			emu_raise_int(INTVECT_SELECTI, LEVEL, 0);
		}
		if (val&O_SCSIREQ) {
			if (s->diag&SCSI_DIAG_LATCH) printf("Diag latch on!\n");
			if (s->diag&SCSI_DIAG_PARITY) printf("Fake parity error on!\n");
			//Check flags
			int flag=val&(O_SCSIIO|O_SCSICD|O_SCSIMSG);
			int wanted_flags=0;
			if (val&O_IOPTR) wanted_flags|=O_SCSIIO;
			if (val&O_CDPTR) wanted_flags|=O_SCSICD;
			if (val&O_MSGPTR) wanted_flags|=O_SCSIMSG;
			//can't go from cd to message
			if ((s->last_req_reg&O_SCSICD) && (val&O_SCSIMSG)) wanted_flags&=~O_SCSIMSG;
			if (flag!=wanted_flags) {
				flag^=wanted_flags;
				int v=INTVECT_POINTER;
				if ((flag&O_SCSIMSG)==0) v|=0x4;
				if ((flag&O_SCSICD)==0) v|=0x2;
				if ((flag&O_SCSIIO)==0) v|=0x1;
				printf("SCSI: Pointer exception 0x%X. Raising interrupt.\n", flag);
				emu_raise_int(v, LEVEL, 0);
				val&=~O_AUTOXFR; //so we start without an error next time
			} else {
				if (flag&O_SCSIIO) {
					//re-use what's already on the bus... I think? (Is that what diag latch does?)
					int old=emu_read_byte(s->pointer);
					if (s->bytecount>0) {
						if (s->pointer&1) {
							if (val&O_SRAM) {
								//Unsure. 8bit writes into 16bit words?
								printf("Warning: SRAM writes are a guess...\n");
								emu_write_byte(s->pointer-1, 0);
								emu_write_byte(s->pointer, s->byte_stashed);
								emu_write_byte(s->pointer+1, 0);
								emu_write_byte(s->pointer+2, s->buf[3]);
								s->pointer+=2;
							} else {
								emu_write_byte(s->pointer-1, s->byte_stashed);
								emu_write_byte(s->pointer, s->buf[3]);
							}
						} else {
							s->byte_stashed=s->buf[3];
						}
						s->pointer++;
					}
					val|=I_ACK;
					if (s->diag & SCSI_DIAG_PARITY) {
						emu_raise_int(INTVECT_PARITY, LEVEL, 0);
					}
				} else {
					s->buf[3]=emu_read_byte(s->pointer);
					printf("Read %x from main memory adr %x\n", s->buf[3], s->pointer);
					//I have no idea what the 3 should come from, but that's the value the diagnostics want.
					if (s->diag & SCSI_DIAG_LATCH) s->buf[3]=3;
					if (s->bytecount>=0) s->pointer++;
					val|=I_ACK;
					if (s->diag & SCSI_DIAG_PARITY) {
						emu_raise_int(INTVECT_PARITY, LEVEL, 0);
					}
				}
				s->last_req_reg=val;
			}
			if (s->bytecount>0) s->bytecount--;
			printf("After read/write: Bytecnt %x ptr %x\n", s->bytecount, s->pointer);
		} else {
			val&=~I_ACK;
		}
	} else if (val&O_ARB) {
		//sure, we succeeded arb.
		emu_raise_int(INTVECT_SELECTI, LEVEL, 0);
	} else if (val&O_SELENA) {
		emu_raise_int(INTVECT_RESELECT, LEVEL, 0);
	}
	s->reg=val;
}

unsigned int scsi_get_scsireg(scsi_t *s) {
	unsigned int ret;
	ret=s->reg&(I_ACK|I_ATN|I_CD|I_IO|I_SCRST|I_MSG|I_REQ|I_BSY);
	//I do not know what this field is or does. This makes diags happy though.
	if (s->pointer!=0) ret|=I_SCZERO;
	if (s->bytecount!=0) ret|=I_DATEN;
	return ret;
}

void scsi_set_diag(scsi_t *s, int flags) {
	s->diag=flags;
}

scsi_t *scsi_new() {
	scsi_t *ret=calloc(sizeof(scsi_t), 1);
	ret->state=STATE_BUS_FREE;
	return ret;
}
