#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include "csr.h"
#include "emu.h"

/*
On the MMU:

We have 2048 entries (well, x2, one set for sys and one for usr)
This is virtual memory; if the address space is 8M, this means
a page size of 4K.

We have space for 8K physical pages; this means we could map
to 32MiB of physical RAM.

*/


typedef struct {
	uint16_t w0;
	uint16_t w1;
} desc_t;

#define SYS_ENTRY_START 2048

//Note: on write of 32-bit, w0 is msb and w1 lsb

#define W1_R 0x8000
#define W1_W 0x4000
#define W1_X 0x2000
#define W1_PAGE_MASK 0x1FFF

#define W0_REFD 0x2
#define W0_ALTRD 0x1
#define W0_UID_SHIFT 8
#define W0_UID_MASK 0xff

typedef struct {
	//2K entries for usr, 2K for sys
	desc_t desc[4096];
	uint8_t *physram;
	int physram_size;
	int sysmode; //indicates if next accesses are in sysmode or not
} mapper_t;

void mapper_write16(void *obj, unsigned int a, unsigned int val) {
	if (emu_get_cur_cpu()==0) return; //seems writes from dma cpu are not allowed

	mapper_t *m=(mapper_t*)obj;
	a=a/2; //word addr
	if (a&1) {
		m->desc[a/2].w1=val;
	} else {
		m->desc[a/2].w0=val;
	}
//	if (a/2==2048) printf("write page %d, w%d. w0=%x, w1=%x\n", a/2, a&1, m->desc[a/2].w0, m->desc[a/2].w1);
}

void mapper_write32(void *obj, unsigned int a, unsigned int val) {
	mapper_write16(obj, a, val>>16);
	mapper_write16(obj, a+2, val&0xffff);
}


unsigned int mapper_read16(void *obj, unsigned int a) {
	mapper_t *m=(mapper_t*)obj;
	a=a/2; //word addr
//	if (a/2==2048) printf("read page %d, w%d. w0=%x, w1=%x\n", a/2, a&1, m->desc[a/2].w0, m->desc[a/2].w1);
	if (a&1) {
		return m->desc[a/2].w1;
	} else {
		return m->desc[a/2].w0;
	}
}

unsigned int mapper_read32(void *obj, unsigned int a) {
	return (mapper_read16(obj,a)<<16)+mapper_read16(obj, a+2);
}

void mapper_set_sysmode(mapper_t *m, int cpu_in_sysmode) {
	m->sysmode=cpu_in_sysmode;
}

int do_map(mapper_t *m, unsigned int a, unsigned int is_write) {
	//Map virtual page to phyical page.
	int p=a>>12; //4K pages
	assert(p<2048);
	if (m->sysmode) p+=SYS_ENTRY_START;

	m->desc[p].w0|=W0_REFD;
	if (is_write) m->desc[p].w0|=W0_ALTRD;

	int phys_p=m->desc[p].w1&W1_PAGE_MASK;
	int phys=(a&0xFFF)|(phys_p<<12);
	assert(phys<8*1024*1024);
//	printf("do_map %s 0x%x to 0x%x, virt page %d phys page %d\n", m->sysmode?"sys":"usr", a, phys, p, phys_p);
	return phys;
}

void mapper_ram_write8(void *obj, unsigned int a, unsigned int val) {
	mapper_t *m=(mapper_t*)obj;
	uint8_t *buffer=m->physram;
	a=do_map(m, a, 1);
	if (a<0) return;
	buffer[a]=val;
}

void mapper_ram_write16(void *obj, unsigned int a, unsigned int val) {
	mapper_t *m=(mapper_t*)obj;
	uint8_t *buffer=m->physram;
	a=do_map(m, a, 1);
	if (a<0) return;
	buffer[a]=(val>>8);
	buffer[a+1]=val;
}

void mapper_ram_write32(void *obj, unsigned int a, unsigned int val) {
	mapper_t *m=(mapper_t*)obj;
	uint8_t *buffer=m->physram;
	a=do_map(m, a, 1);
	if (a<0) return;
	buffer[a]=(val>>24);
	buffer[a+1]=(val>>16);
	buffer[a+2]=(val>>8);
	buffer[a+3]=val;
}

unsigned int mapper_ram_read8(void *obj, unsigned int a) {
	mapper_t *m=(mapper_t*)obj;
	uint8_t *buffer=m->physram;
	a=do_map(m, a, 0);
	if (a<0) return 0;
	return buffer[a];
}

unsigned int mapper_ram_read16(void *obj, unsigned int a) {
	mapper_t *m=(mapper_t*)obj;
	uint8_t *buffer=m->physram;
	a=do_map(m, a, 0);
	if (a<0) return 0;
	return buffer[a+1]+(buffer[a]<<8);
}

unsigned int mapper_ram_read32(void *obj, unsigned int a) {
	mapper_t *m=(mapper_t*)obj;
	uint8_t *buffer=m->physram;
	a=do_map(m, a, 0);
	if (a<0) return 0;
	return buffer[a+3]+(buffer[a+2]<<8)+(buffer[a+1]<<16)+(buffer[a]<<24);
}

mapper_t *mapper_new(void *physram, int size) {
	mapper_t *ret=calloc(sizeof(mapper_t), 1);
	ret->physram=(uint8_t*)physram;
	ret->physram_size=size;
	return ret;
}


