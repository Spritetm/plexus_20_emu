#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <stdbool.h>
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

On write of 32-bit, w0 is msb and w1 lsb.  This happens because our
CPU emulator supports a 32-bit bus; the 68010 in the Plexus P/20 had
only a 16-bit bus so there were two read/write cycles.

Note the RWX bits *disable* that access when 1, ie they are "prohibited"
bits not allow bits.


Documentation from the Plexus P/20 specification:

All accesses to main memory go through the map circuit as mentioned
previously. This function is described here.

The map circuit performs address translation plus access privilege
checking for each page in memory.

The map registers are addressable only by the Job Processor in system
space 16 bits at a time.

The addresss are 900000 thru 903FFF (9 0 00up pppp pppp ppw0).

The addressing for the map is given below:

Address decode:

u           = 1 if page is in system space
ppppppppppp = page number
w           = Word select

Data decode:

Word 1 = rwxn nnnn nnnn nnnn
         r   = 0, read enable
          w  = 0, write enable
           x = 0, execute enable
            n nnnn nnnn nnnn = physical page number

Word 0 = iiii iiii xxxx xxrd
         iiii iiii = user id
                          r  = 1, set when page is referenced
                           d = 1, set when page is altered
*/

typedef struct {
	uint16_t w0;
	uint16_t w1;
} desc_t;

#define SYS_ENTRY_START 2048

#define W1_PAGE_MASK 0x1FFF
#define W1_INVALID_PAGE 0xFFF

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

//returns fault indicator, or 0 if allowed
static int access_allowed_page(mapper_t *m, unsigned int page, int access_flags, unsigned int a) {
	assert(page<4096);

	int fault=((m->desc[page].w1&access_flags)&(ACCESS_R|ACCESS_W|ACCESS_X)) << 16;
	int uid=(m->desc[page].w0>>W0_UID_SHIFT)&W0_UID_MASK;

	// mapper UID checks seem to only apply to writes, not read/execute
	if (((access_flags&ACCESS_SYSTEM)==0) && (access_flags&ACCESS_W)) {
		if (uid != m->cur_id) fault|=(uid<<8|0xff);
	}
	if (fault) {
		// PFN 0xfff and no-R/no-W/no-X is for invalid page
		// (NOTE: 0x1fff appears not to be used here, which would be more obvious)
		bool invalid_page_map = ((m->desc[page].w1&ACCESS_R) &&
					 (m->desc[page].w1&ACCESS_W) &&
					 (m->desc[page].w1&ACCESS_X) &&
					 ((m->desc[page].w1&W1_PAGE_MASK) == W1_INVALID_PAGE));

		MAPPER_LOG_DEBUG("Mapper: Access fault: address %08x, page %04x ent "
				 "w0=%04x, w1=%04x (page_perm=%c%c%c), %s"
				 "req %x (req_perm=%c%c%c), %s, fault %x (",
				a, page, m->desc[page].w0, m->desc[page].w1,
				((m->desc[page].w1&ACCESS_R)?'-':'R'), // page flags: 1 if *blocked*
				((m->desc[page].w1&ACCESS_W)?'-':'W'),
				((m->desc[page].w1&ACCESS_X)?'-':'X'),
				(invalid_page_map?"not mapped, ":""),
				access_flags,
				((access_flags&ACCESS_R)?'R':'-'),     // req flags: 1 if requested
				((access_flags&ACCESS_W)?'W':'.'),
				((access_flags&ACCESS_X)?'X':'-'),
				((access_flags&ACCESS_SYSTEM)?"system":"user"),
				fault);
		if (fault&(ACCESS_W<<16)) MAPPER_LOG_DEBUG("write violation ");
		if (fault&(ACCESS_R<<16)) MAPPER_LOG_DEBUG("read violation ");
		if (fault&(ACCESS_X<<16)) MAPPER_LOG_DEBUG("execute violation ");
		MAPPER_LOG_DEBUG("proc uid %d %s page uid %d", uid, (m->cur_id==uid?"=":"!="), m->cur_id);
		MAPPER_LOG_DEBUG(")\n");
	}
	return fault;
}

int mapper_access_allowed(mapper_t *m, unsigned int a, int access_flags) {
	if (a>=0x800000) {
		//Anything except RAM does not go through the mapper, but is only
		//accessible in system mode.
		int ret=(access_flags&ACCESS_SYSTEM)?ACCESS_ERROR_OK:ACCESS_ERROR_A;
		if (ret==ACCESS_ERROR_A) {
			MAPPER_LOG_INFO("mapper_access_allowed: address %x not accessible in user mode\n", a);
		}
		return ret;
	}
	//Map virtual page to phyical page.
	int p=a>>12; //4K pages
	assert(p<=2048 && "out of range addr");
	if (access_flags&ACCESS_SYSTEM) p+=2048;
	int r=access_allowed_page(m, p, access_flags, a);
	if (r && log_level_active(LOG_SRC_MAPPER, LOG_DEBUG)) {
		MAPPER_LOG_DEBUG("Mapper: Access fault at addr %x page %d. CPU state:\n", a, p);
		dump_cpu_state();
		dump_callstack();
		MAPPER_LOG_DEBUG("Mapper: Dump done.\n");
	}
	int x=ACCESS_ERROR_OK;
	if (r) x=ACCESS_ERROR_A;
	if (r&0xff00) x=ACCESS_ERROR_U;
	return x;
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
	if (a/2==2048) MAPPER_LOG_DEBUG("write page %d, w%d. w0=%04x, w1=%04x\n", a/2, a&1, m->desc[a/2].w0, m->desc[a/2].w1);
}

void mapper_write32(void *obj, unsigned int a, unsigned int val) {
	mapper_write16(obj, a, val>>16);
	mapper_write16(obj, a+2, val&0xffff);
}


unsigned int mapper_read16(void *obj, unsigned int a) {
	mapper_t *m=(mapper_t*)obj;
	a=a/2; //word addr
	if (a/2==2048) MAPPER_LOG_DEBUG("read page %d, w%d. w0=%04x, w1=%04x\n", a/2, a&1, m->desc[a/2].w0, m->desc[a/2].w1);
	if (a&1) {
		return m->desc[a/2].w1;
	} else {
		return m->desc[a/2].w0;
	}
}

void mapper_write8(void *obj, unsigned int a, unsigned int val) {
	int v=mapper_read16(obj, a&~1);
	if (a&1) {
		v=(v&0xFF00)|val;
	} else {
		v=(v&0xFF)|(val<<8);
	}
	mapper_write16(obj, a&~1, v);
}

unsigned int mapper_read8(void *obj, unsigned int a) {
	int v=mapper_read16(obj, a&~1);
	if (a&1) {
		return v&0xff;
	} else {
		return v>>8;
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
//	MAPPER_LOG_DEBUG("do_map %s 0x%x to 0x%x, virt page %d phys page %d\n", m->sysmode?"sys":"usr", a, phys, p, phys_p);
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


