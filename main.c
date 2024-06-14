#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include "Musashi/m68k.h"
#include "uart.h"

typedef struct mem_range_t mem_range_t;

typedef unsigned int (*read_cb)(void *obj, unsigned int addr);
typedef void (*write_cb)(void *obj, unsigned int addr, unsigned int val);

struct mem_range_t {
	const char *name;
	uint32_t offset;
	uint32_t size;
	void *obj;
	read_cb read8;
	read_cb read16;
	read_cb read32;
	write_cb write8;
	write_cb write16;
	write_cb write32;
};

static mem_range_t memory[]={
	{.name="ROMSHDW", .offset=0, .size=0}, ///used for boot
	{.name="RAM",     .offset=0, .size=0x800000},
	{.name="U17",     .offset=0x800000, .size=0x8000}, //used to be U19
	{.name="U15",     .offset=0x808000, .size=0x8000}, //used to be U17
	{.name="USRPAGE", .offset=0x900000, .size=0x2000},
	{.name="SYSPAGE", .offset=0x902000, .size=0x2000},
	{.name="UART_A",  .offset=0xA00000, .size=0x40},
	{.name="UART_B",  .offset=0xa10000, .size=0x40},
	{.name="UART_C",  .offset=0xa20000, .size=0x40},
	{.name="UART_D",  .offset=0xa30000, .size=0x40},
	{.name="SCSIBUF", .offset=0xa70000, .size=0x4},
	{.name="MBUSIO",  .offset=0xb00000, .size=0x80000},
	{.name="MBUSMEM", .offset=0xb80000, .size=0x80000},
	{.name="SRAM",    .offset=0xc00000, .size=0x4000},
	{.name="RTC",     .offset=0xd00000, .size=0x1c},
	{.name="RTC_RAM", .offset=0xd0001c, .size=0x64},
	{.name="CSR",     .offset=0xe00000, .size=0x20},
	{.name="MMIO_WR", .offset=0xe00020, .size=0x1e0},
	{.name="VECTORS", .offset=0xf00000, .size=0x10},
	{.name=NULL}
};

void dump_cpu_state() {
	unsigned int pc=m68k_get_reg(NULL, M68K_REG_PC);
	unsigned int sp=m68k_get_reg(NULL, M68K_REG_SP);
	unsigned int sr=m68k_get_reg(NULL, M68K_REG_SR);
	printf("PC 0x%08X SP 0x%08X SR 0x%08X\n", pc, sp, sr);
}

static mem_range_t *find_range_by_name(const char *name) {
	int i=0;
	while (memory[i].name!=NULL) {
		if (strcmp(memory[i].name, name)==0) return &memory[i];
		i++;
	}
	return NULL;
}

//There's faster ways to do this. We don't use them for now.
static mem_range_t *find_range_by_addr(unsigned int addr) {
	int i=0;
	while (memory[i].name!=NULL) {
		if (addr>=memory[i].offset && addr<memory[i].offset+memory[i].size) {
			return &memory[i];
		}
		i++;
	}
	return NULL;
}

void ram_write8(void *obj, unsigned int a, unsigned int val) {
	uint8_t *buffer=(uint8_t*)obj;
	buffer[a]=val;
}

void ram_write16(void *obj, unsigned int a, unsigned int val) {
	uint8_t *buffer=(uint8_t*)obj;
	buffer[a]=(val>>8);
	buffer[a+1]=val;
}

void ram_write32(void *obj, unsigned int a, unsigned int val) {
	uint8_t *buffer=(uint8_t*)obj;
	buffer[a]=(val>>24);
	buffer[a+1]=(val>>16);
	buffer[a+2]=(val>>8);
	buffer[a+3]=val;
}

unsigned int ram_read8(void *obj, unsigned int a) {
	uint8_t *buffer=(uint8_t*)obj;
	return buffer[a];
}

unsigned int ram_read16(void *obj, unsigned int a) {
	uint8_t *buffer=(uint8_t*)obj;
	return buffer[a+1]+(buffer[a]<<8);
}

unsigned int ram_read32(void *obj, unsigned int a) {
	uint8_t *buffer=(uint8_t*)obj;
	return buffer[a+3]+(buffer[a+2]<<8)+(buffer[a+1]<<16)+(buffer[a]<<24);
}

void setup_ram(const char *name) {
	mem_range_t *m=find_range_by_name(name);
	assert(m);
	m->obj=calloc(m->size, 1);
	m->read8=ram_read8;
	m->read16=ram_read16;
	m->read32=ram_read32;
	m->write8=ram_write8;
	m->write16=ram_write16;
	m->write32=ram_write32;
	printf("Set up 0x%X bytes of RAM in section '%s'.\n", m->size, m->name);
}

void setup_rom(const char *name, const char *filename) {
	mem_range_t *m=find_range_by_name(name);
	assert(m);
	FILE *f=fopen(filename, "rb");
	if (!f) {
		perror(filename);
		exit(1);
	}
	m->obj=malloc(m->size);
	int r=fread(m->obj, 1, m->size, f);
	fclose(f);
	if (r!=m->size) {
		printf("%s: %d bytes for rom region of %d bytes\n", m->name, r, m->size);
	}
	m->read8=ram_read8;
	m->read16=ram_read16;
	m->read32=ram_read32;
	printf("Loaded ROM '%s' into section '%s' at addr %x\n", filename, name, m->offset);
}

#define PRINT_MEMREAD 0

unsigned int  m68k_read_memory_32(unsigned int address) {
	mem_range_t *m=find_range_by_addr(address);
	//HACK! If this is set, diags get more verbose
	if (address==0xC00644) return 1;
	if (!m) {
		printf("Read32 from unmapped addr %08X\n", address);
		dump_cpu_state();
		return 0xdeadbeef;
	}
	if (!m->read32) {
		if (m->read8) {
			unsigned int r;
			r=m->read8(m->obj, address - m->offset)<<24;
			r+=m->read8(m->obj, address - m->offset+1)<<16;
			r+=m->read8(m->obj, address - m->offset+2)<<8;
			r+=m->read8(m->obj, address - m->offset+3);
			return r;
		} else {
			printf("No read32/read8 implemented for '%s', addr 0x%08X\n", m->name, address);
			dump_cpu_state();
			return 0xdeadbeef;
		}
	}
#if PRINT_MEMREAD
	printf("read32 %s %x -> %x\n", m->name, address, m->read32(m->obj, address - m->offset));
#endif
	return m->read32(m->obj, address - m->offset);
}
unsigned int  m68k_read_memory_16(unsigned int address) {
	mem_range_t *m=find_range_by_addr(address);
	if (!m) {
		printf("Read16 from unmapped addr %08X\n", address);
		dump_cpu_state();
		return 0xbeef;
	}
	if (!m->read16) {
		if (m->read8) {
			unsigned int r;
			r=m->read8(m->obj, address - m->offset)<<8;
			r+=m->read8(m->obj, address - m->offset+1);
			return r;
		} else {
			printf("No read16/read8 implemented for '%s', addr 0x%08X\n", m->name, address);
		dump_cpu_state();
			return 0xbeef;
		}
	}
#if PRINT_MEMREAD
	printf("read16 %s %x -> %x\n", m->name, address, m->read16(m->obj, address - m->offset));
#endif
	return m->read16(m->obj, address - m->offset);
}

unsigned int m68k_read_memory_8(unsigned int address) {
	mem_range_t *m=find_range_by_addr(address);
	if (!m) {
		printf("Read8 from unmapped addr %08X\n", address);
		dump_cpu_state();
		return 0x5a;
	}
	if (!m->read8) {
		printf("No read8 implemented for '%s', addr 0x%08X\n", m->name, address);
		dump_cpu_state();
		return 0x5a;
	}
#if PRINT_MEMREAD
	printf("read8 %s %x -> %x\n", m->name, address, m->read8(m->obj, address - m->offset));
#endif
	return m->read8(m->obj, address - m->offset);
}


void m68k_write_memory_8(unsigned int address, unsigned int value) {
	mem_range_t *m=find_range_by_addr(address);
	if (!m) {
		printf("Write8 to unmapped addr %08X data 0x%X\n", address, value);
		return;
	}
	if (!m->write8) {
		printf("No write8 implementation for %s, addr 0x%08X, data 0x%X\n", m->name, address, value);
		return;
	}
	m->write8(m->obj, address - m->offset, value);
}

void m68k_write_memory_16(unsigned int address, unsigned int value) {
	mem_range_t *m=find_range_by_addr(address);
	if (!m) {
		printf("Write16 to unmapped addr %08X data 0x%X\n", address, value);
		dump_cpu_state();
		return;
	}
	if (!m->write16) {
		printf("No write16 implementation for %s, addr 0x%08X, data 0x%X\n", m->name, address, value);
		dump_cpu_state();
		return;
	}
	m->write16(m->obj, address - m->offset, value);
}

void m68k_write_memory_32(unsigned int address, unsigned int value) {
	mem_range_t *m=find_range_by_addr(address);
	if (!m) {
		printf("Write32 to unmapped addr %08X data 0x%X\n", address, value);
		dump_cpu_state();
		return;
	}
	if (!m->write32) {
		printf("No write32 implementation for '%s', addr 0x%08X, data 0x%X\n", m->name, address, value);
		dump_cpu_state();
		return;
	}
	m->write32(m->obj, address - m->offset, value);
}

void setup_uart(const char *name, int is_console) {
	mem_range_t *m=find_range_by_name(name);
	uart_t *u=uart_new(name, is_console);
	m->obj=u;
	m->write8=uart_write8;
	m->read8=uart_read8;
}

//csr: usr/include/sys/mtpr.h, note we're Robin

void csr_write16(void *obj, unsigned int a, unsigned int val) {
	if (a==0) { //reset sel
		printf("csr write16 %x (reset sel) val %x\n", a, val);
	} else if (a==6) { //scsi byte count
	} else if (a==0xA) { //scsi pointer reg
	} else if (a==0xe) { //scsi reg
	} else if (a==0x10) { //led regs
	} else if (a==0x18) { //kill
		printf("csr write16 %x (kill) val %x\n", a, val);
	} else {
//		printf("csr write16 %x val %x\n", a, val);
	}
}

void csr_write8(void *obj, unsigned int a, unsigned int val) {
//	printf("csr write8 %x val %x\n", a, val);
	//fake with a csr write16
	csr_write16(obj, a, val);
}


unsigned int csr_read16(void *obj, unsigned int a) {
//	printf("csr read16 %x\n", a);
	if (a==0x18) {
		//note: return 0x8000 if we are the job cpu
		return 0x400;
	}
	return 0;
}

unsigned int csr_read8(void *obj, unsigned int a) {
	//fake using read16
	return csr_read16(obj, a-1)>>8;
}

unsigned int nop_read(void *obj, unsigned int a) {
	return 0;
}

void nop_write(void *obj, unsigned int a, unsigned int val) {
}

void setup_csr(const char *name) {
	mem_range_t *m=find_range_by_name(name);
	m->write8=csr_write8;
	m->write16=csr_write16;
	m->read16=csr_read16;
	m->read8=csr_read8;
}


void setup_nop(const char *name) {
	mem_range_t *m=find_range_by_name(name);
	m->write8=nop_write;
	m->write16=nop_write;
	m->write32=nop_write;
	m->read8=nop_read;
	m->read16=nop_read;
	m->read32=nop_read;
}

uint8_t vectors[256]={0};

int m68k_int_cb(int level) {
	int r=0xf; //unset int exc
	int more_ints=0;
	for (int i=0x10; i<256; i++) {
		if (vectors[i]) {
			if (r==0xf) {
				vectors[i]=0;
				r=i;
			} else {
				more_ints=1;
			}
		}
	}
	if (!more_ints) m68k_set_irq(0);
	printf("Int ack %x\n", r);
	return r;
}

void raise_int(uint8_t vector) {
	printf("Interrupt raised: %x\n", vector);
	vectors[vector]=1;
	dump_cpu_state();
	m68k_set_irq(0);
	m68k_set_irq(7);
	dump_cpu_state();
}

int main(int argc, char **argv) {
	setup_ram("RAM");
	setup_ram("SRAM");
	setup_ram("RTC_RAM");
	setup_ram("USRPAGE");
	setup_ram("SYSPAGE");
	setup_rom("U15", "../plexus-p20/ROMs/U15-MERGED.BIN"); //used to be U17
	setup_rom("U17", "../plexus-p20/ROMs/U17-MERGED.BIN"); //used to be U19
	setup_uart("UART_A", 1);
	setup_uart("UART_B", 0);
	setup_uart("UART_C", 0);
	setup_uart("UART_D", 0);
	setup_csr("CSR");
	setup_nop("MBUSIO");
	setup_nop("MBUSMEM");

	//set up ROM shadow for boot
	mem_range_t *m=find_range_by_name("ROMSHDW");
	mem_range_t *rom=find_range_by_name("U17");
	printf("%x\n", rom->offset);
	m->obj=rom->obj;
	m->read8=rom->read8;
	m->read16=rom->read16;
	m->read32=rom->read32;
	m->size=rom->size;

	m68k_set_cpu_type(M68K_CPU_TYPE_68010);
	m68k_init();
	//note: cbs should happen after init
	m68k_set_int_ack_callback(m68k_int_cb);
	m68k_pulse_reset();
	m68k_set_irq(0);
	dump_cpu_state();

	m68k_execute(10);
	m->size=0; //disable shadow rom

	while(1) {
		m68k_execute(10);
//		dump_cpu_state();
	}
	return 0;
}


