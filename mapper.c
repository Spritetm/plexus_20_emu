#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include "csr.h"
#include "emu.h"
#include "log.h"
#include "mapper.h"

// Debug logging
#define MAPPER_LOG(msg_level, format_and_args...) \
	log_printf(LOG_SRC_MAPPER, msg_level, format_and_args)
#define MAPPER_LOG_DEBUG(format_and_args...) MAPPER_LOG(LOG_DEBUG, format_and_args)
#define MAPPER_LOG_INFO(format_and_args...) MAPPER_LOG(LOG_INFO, format_and_args)

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

//Note the RWX bits *disable* that access when 1.
#define W1_R 0x8000
#define W1_W 0x4000
#define W1_X 0x2000
#define W1_PAGE_MASK 0x1FFF

#define W0_REFD 0x2
#define W0_ALTRD 0x1
#define W0_UID_SHIFT 8
#define W0_UID_MASK 0xff

struct mapper_t {
	//2K entries for usr, 2K for sys
	desc_t desc[4096];
	uint8_t *physram;
	int physram_size;
	int sysmode; //indicates if next accesses are in sysmode or not
	int cur_id;
};

void mapper_set_mapid(mapper_t *m, uint8_t id) {
	if (m->cur_id!=id) MAPPER_LOG_DEBUG("Switching to map id %d\n", id);
	m->cur_id=id;
}

static int access_allowed_page(mapper_t *m, unsigned int page, int access_flags) {
	assert(page<4096);
	unsigned int ac=(m->desc[page].w1<<16)+m->desc[page].w0;
	int fault=(ac&access_flags)&(ACCESS_R|ACCESS_W|ACCESS_X);
	//todo: also check uid properly
	int uid=(ac>>W0_UID_SHIFT)&W0_UID_MASK;
	if (uid != m->cur_id) fault=(uid<<8|0xff);
	if (fault) MAPPER_LOG_DEBUG("Mapper: Access fault at page %d, page addr %x, fault %x (page ent %x req %x)\n", page, (page&2047)<<12, fault, ac, access_flags);
	return !fault;
}

int mapper_access_allowed(mapper_t *m, unsigned int a, int access_flags) {
	//we only check RAM
	if (a>=0x800000) return 1;
	//Map virtual page to phyical page.
	int p=a>>12; //4K pages
	if (p>=2048) {
		MAPPER_LOG_INFO("mapper_access_allowed: out of range addr %x\n", a);
		exit(1);
	}
	if (access_flags&ACCESS_SYSTEM) p+=2048;
	return access_allowed_page(m, p, access_flags);
}


void mapper_write16(void *obj, unsigned int a, unsigned int val) {
	if (emu_get_cur_cpu()==0) return; //seems writes from dma cpu are not allowed

	mapper_t *m=(mapper_t*)obj;
	a=a/2; //word addr
	if (a&1) {
		m->desc[a/2].w1=val;
	} else {
		m->desc[a/2].w0=val;
	}
	if (a/2==2048) MAPPER_LOG_DEBUG("write page %d, w%d. w0=%x, w1=%x\n", a/2, a&1, m->desc[a/2].w0, m->desc[a/2].w1);
}

void mapper_write32(void *obj, unsigned int a, unsigned int val) {
	mapper_write16(obj, a, val>>16);
	mapper_write16(obj, a+2, val&0xffff);
}


unsigned int mapper_read16(void *obj, unsigned int a) {
	mapper_t *m=(mapper_t*)obj;
	a=a/2; //word addr
	if (a/2==2048) MAPPER_LOG_DEBUG("read page %d, w%d. w0=%x, w1=%x\n", a/2, a&1, m->desc[a/2].w0, m->desc[a/2].w1);
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
	phys&=((8*1024*1024)-1);
//	assert(phys<8*1024*1024);
	MAPPER_LOG_DEBUG("do_map %s 0x%x to 0x%x, virt page %d phys page %d\n", m->sysmode?"sys":"usr", a, phys, p, phys_p);
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


