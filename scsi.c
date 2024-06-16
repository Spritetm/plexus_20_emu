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
};

void scsi_write16(void *obj, unsigned int a, unsigned int val) {
	scsi_t *c=(scsi_t*)obj;
	c->buf[a]=val>>8;
	c->buf[a+1]=val;
}

void scsi_write8(void *obj, unsigned int a, unsigned int val) {
	scsi_t *c=(scsi_t*)obj;
	c->buf[a]=val;
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

scsi_t *scsi_new() {
	scsi_t *ret=calloc(sizeof(scsi_t), 1);
	return ret;
}


void scsi_set_bytecount(scsi_t *s, int bytecount) {
	s->bytecount=bytecount;
}

void scsi_set_pointer(scsi_t *s, int pointer) {
	s->pointer=pointer;
}

#define O_IOPTR		0x8000
#define O_MSGPTR	0x4000
#define O_CDPTR		0x2000
#define O_SRAM		0x1000
#define O_RESET		0x0800
#define O_SELENA	0x0400
#define O_NSCSIBSY	0x0200
#define O_ARB		0x0100
#define O_NSCSIREQ	0x0080
#define O_NSCSIMSG	0x0040
#define O_NSCSIRST	0x0020
#define O_SCSIIO	0x0010
#define O_SCSICD	0x0008
#define O_NSCSIATN	0x0004
#define O_NSCSIACK	0x0002
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

void scsi_set_scsireg(scsi_t *s, unsigned int val) {
	s->reg=val;
}

unsigned int scsi_get_scsireg(scsi_t *s) {
	return s->reg;
}


