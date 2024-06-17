#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include "Musashi/m68k.h"
#include "uart.h"
#include "ramrom.h"
#include "csr.h"
#include "mapper.h"
#include "scsi.h"
#include "mbus.h"
#include "rtc.h"

typedef struct mem_range_t mem_range_t;

typedef unsigned int (*read_cb)(void *obj, unsigned int addr);
typedef void (*write_cb)(void *obj, unsigned int addr, unsigned int val);

FILE *tracefile=NULL;
int do_tracefile=0;

int insn_id=0;

int cur_cpu;


int32_t callstack[2][8192];
int callstack_ptr[2]={0};

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
//	{.name="RAM",     .offset=0, .size=0x200000}, //only 2MiB of RAM
	{.name="RAM",     .offset=0, .size=0x800000}, //fully decked out with 8MiB of RAM
	{.name="MAPRAM",  .offset=0, .size=0}, //MMU-mapped RAM
	{.name="U17",     .offset=0x800000, .size=0x8000}, //used to be U19
	{.name="U15",     .offset=0x808000, .size=0x8000}, //used to be U17
	{.name="MAPPER",  .offset=0x900000, .size=0x4000},
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

int trace_enabled=0;

void dump_cpu_state() {
	//note REG_PPC is previous PC, aka the currently executing insn
	unsigned int pc=m68k_get_reg(NULL, M68K_REG_PPC);
	unsigned int sp=m68k_get_reg(NULL, M68K_REG_SP);
	unsigned int sr=m68k_get_reg(NULL, M68K_REG_SR);

	unsigned int d0=m68k_get_reg(NULL, M68K_REG_D0);

	unsigned int mem=m68k_read_memory_8(0xc00604);

	printf("id %d CPU %d PC %08X SP %08X SR %08X D0 %08X m %08X\n", insn_id, cur_cpu, pc, sp, sr, d0, mem);
}

void dump_callstack() {
	printf("Callstack (CPU %d): ", cur_cpu);
	for (int i=callstack_ptr[cur_cpu]-1; i>=0; --i) {
		printf("%06X ", callstack[cur_cpu][i]);
	}
	printf("\n");
}


static void watch_write(unsigned int addr, unsigned int val, int len);

static mem_range_t *find_range_by_name(const char *name) {
	int i=0;
	while (memory[i].name!=NULL) {
		if (strcmp(memory[i].name, name)==0) return &memory[i];
		i++;
	}
	return NULL;
}

//There's faster ways to do this. We don't implement them for now.
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

void setup_ram(const char *name) {
	mem_range_t *m=find_range_by_name(name);
	assert(m);
	m->obj=ram_new(m->size);
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
	m->obj=rom_new(filename, m->size);
	m->read8=ram_read8;
	m->read16=ram_read16;
	m->read32=ram_read32;
	printf("Loaded ROM '%s' into section '%s' at addr %x\n", filename, name, m->offset);
}

#define PRINT_MEMREAD 0

unsigned int m68k_read_memory_32(unsigned int address) {
//	if (address==0) printf("read addr 0\n");
	mem_range_t *m=find_range_by_addr(address);
	//HACK! If this is set, diags get more verbose
	if (address==0xC00644) return 1;
	if (address==0xC006de) return 1;
	if (!m) {
		printf("Read32 from unmapped addr %08X\n", address);
		dump_cpu_state();
		return 0xdeadbeef;
	}
	if (!m->read32) {
		printf("No read32 implemented for '%s', addr 0x%08X\n", m->name, address);
		dump_cpu_state();
		return 0xdeadbeef;
	}
#if PRINT_MEMREAD
	printf("read32 %s %x -> %x\n", m->name, address, m->read32(m->obj, address - m->offset));
#endif
	return m->read32(m->obj, address - m->offset);
}
unsigned int m68k_read_memory_16(unsigned int address) {
	mem_range_t *m=find_range_by_addr(address);
	if (!m) {
		printf("Read16 from unmapped addr %08X\n", address);
		dump_cpu_state();
		return 0xbeef;
	}
	if (!m->read16) {
		printf("No read16 implemented for '%s', addr 0x%08X\n", m->name, address);
		dump_cpu_state();
		return 0xbeef;
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
	watch_write(address, value, 8);
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
	watch_write(address, value, 16);
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
	watch_write(address, value, 32);
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


//Used for SCSI DMA transfers
//ToDo: do these go through the mapper?
int emu_read_byte(int addr) {
	return m68k_read_memory_8(addr);
}

void emu_write_byte(int addr, int val) {
	m68k_write_memory_8(addr, val);
}

uart_t *setup_uart(const char *name, int is_console) {
	mem_range_t *m=find_range_by_name(name);
	uart_t *u=uart_new(name, is_console);
	m->obj=u;
	m->write8=uart_write8;
	m->read8=uart_read8;
	return u;
}

unsigned int nop_read(void *obj, unsigned int a) {
	return 0;
}

void nop_write(void *obj, unsigned int a, unsigned int val) {
}

void setup_scsi(const char *name) {
	mem_range_t *m=find_range_by_name(name);
	m->obj=scsi_new();
	m->write8=scsi_write8;
	m->write16=scsi_write16;
	m->read16=scsi_read16;
	m->read8=scsi_read8;
}

rtc_t *setup_rtc(const char *name) {
	mem_range_t *m=find_range_by_name(name);
	rtc_t *r=rtc_new();
	m->obj=r;
	m->write8=rtc_write8;
	m->write16=rtc_write16;
	m->read8=rtc_read8;
	m->read16=rtc_read16;
	return r;
}

csr_t *setup_csr(const char *name, const char *mmio_name, const char *scsi_name) {
	mem_range_t *m=find_range_by_name(name);
	mem_range_t *mm=find_range_by_name(mmio_name);
	mem_range_t *sc=find_range_by_name(scsi_name);
	csr_t *r=csr_new((scsi_t*)sc->obj);
	m->obj=r;
	mm->obj=r;
	m->write8=csr_write8;
	m->write16=csr_write16;
	m->write32=csr_write32;
	m->read32=csr_read32;
	m->read16=csr_read16;
	m->read8=csr_read8;
	mm->write16=csr_write16_mmio;
	return r;
}

mapper_t *setup_mapper(const char *name, const char *mapram, const char *physram) {
	mem_range_t *m=find_range_by_name(name);
	mem_range_t *mr=find_range_by_name(mapram);
	mem_range_t *pr=find_range_by_name(physram);

	mapper_t *map=mapper_new(pr->obj, pr->size);
	m->obj=map;
	m->write32=mapper_write32;
	m->write16=mapper_write16;
	m->read32=mapper_read32;
	m->read16=mapper_read16;

	mr->obj=map;
	mr->read8=mapper_ram_read8;
	mr->read16=mapper_ram_read16;
	mr->read32=mapper_ram_read32;
	mr->write8=mapper_ram_write8;
	mr->write16=mapper_ram_write16;
	mr->write32=mapper_ram_write32;
	return map;
}

void emu_enable_mapper(int do_enable) {
	mem_range_t *r=find_range_by_name("RAM");
	mem_range_t *mr=find_range_by_name("MAPRAM");
	if (do_enable) {
		if (r->size!=0) {
			mr->size=r->size;
			r->size=0;
//			printf("Mapper ENABLED\n");
		}
	} else {
		if (mr->size!=0) {
			r->size=mr->size;
			mr->size=0;
//			printf("Mapper DISABLED\n");
		}
	}
}

//Note this does not work as mbus accesses go through the mapper.
void setup_mbus(const char *name) {
	mem_range_t *m=find_range_by_name(name);
	//note mbus needs no obj
	m->read8=mbus_read8;
	m->read16=mbus_read16;
	m->read32=mbus_read32;
	m->write8=mbus_write8;
	m->write16=mbus_write16;
	m->write32=mbus_write32;
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

mapper_t *mapper;

void m68k_fc_cb(unsigned int fc) {
	mapper_set_sysmode(mapper, fc&4);
}

//has a level if triggered, otherwise 0
uint8_t vectors[2][256]={0};


//note: acts on currently active cpu
static void raise_highest_int() {
	int highest_lvl=0;
	for (int i=0x10; i<256; i++) {
		if (highest_lvl<vectors[cur_cpu][i]) {
			highest_lvl=vectors[cur_cpu][i];
		}
	}
	m68k_set_irq(highest_lvl);
}

int m68k_int_cb(int level) {
	int r=0xf; //unset int exc
	for (int i=0x10; i<256; i++) {
		if (vectors[cur_cpu][i]==level) {
			r=i;
		}
	}
	vectors[cur_cpu][r]=0;
	raise_highest_int();
//	printf("Int ack %x\n", r);
	return r;
}

int need_raise_highest_int[2]={0};

void emu_raise_int(uint8_t vector, uint8_t level, int cpu) {
//	printf("Interrupt raised: %x\n", vector);
	vectors[cpu][vector]=level;
	need_raise_highest_int[cpu]=1;
//	do_tracefile=1;
}



void m68k_trace_cb(unsigned int pc) {
	insn_id++;
	//note: pc already is advanced to the next insn when this is called
	//but ir is not
	static unsigned int prev_pc=0;
	unsigned int ir=m68k_get_reg(NULL, M68K_REG_IR);
	//decode jsr/trs instructions for callstack tracing
	if ((ir&0xFFC0)==0x4e80) callstack[cur_cpu][callstack_ptr[cur_cpu]++]=prev_pc;
	if (ir==0x4E75) callstack_ptr[cur_cpu]--;
	prev_pc=pc;
	if (do_tracefile && cur_cpu==1) fprintf(tracefile, "%d %d %06x\n", insn_id, cur_cpu, pc);
	if (!trace_enabled) return;
	dump_cpu_state();
}


static void watch_write(unsigned int addr, unsigned int val, int len) {
	if (addr/4==-1) {
		dump_callstack();
	} else {
		return;
	}
	dump_cpu_state();
	printf("At ^^: Watch addr %06X changed to %08X\n", addr, val);
}

int emu_get_cur_cpu() {
	return cur_cpu;
}

int main(int argc, char **argv) {
	tracefile=fopen("trace.txt","w");
	setup_ram("RAM");
	setup_ram("SRAM");
	setup_ram("RTC_RAM");
	setup_rom("U15", "../plexus-p20/ROMs/U15-MERGED.BIN"); //used to be U17
	setup_rom("U17", "../plexus-p20/ROMs/U17-MERGED.BIN"); //used to be U19
	uart_t *uart[4];
	uart[0]=setup_uart("UART_A", 1);
	uart[1]=setup_uart("UART_B", 0);
	uart[2]=setup_uart("UART_C", 0);
	uart[3]=setup_uart("UART_D", 0);
	setup_scsi("SCSIBUF");
	csr_t *csr=setup_csr("CSR", "MMIO_WR", "SCSIBUF");
	mapper=setup_mapper("MAPPER", "MAPRAM", "RAM");
//	setup_nop("MBUSIO");
	setup_mbus("MBUSMEM");
	rtc_t *rtc=setup_rtc("RTC");

	//set up ROM shadow for boot
	//technically, there's a bit in the CSR that forces bit 23 of the address high
	//so this is a hack. ToDo: maybe implement properly.
	mem_range_t *m=find_range_by_name("ROMSHDW");
	mem_range_t *rom=find_range_by_name("U17");
	printf("%x\n", rom->offset);
	m->obj=rom->obj;
	m->read8=rom->read8;
	m->read16=rom->read16;
	m->read32=rom->read32;
	m->size=rom->size;

	void *cpuctx[2];
	cpuctx[0]=calloc(m68k_context_size(), 1); //dma cpu
	cpuctx[1]=calloc(m68k_context_size(), 1); //job cpu

	for (int i=0; i<2; i++) {
		m68k_set_context(cpuctx[i]);
		m68k_set_cpu_type(M68K_CPU_TYPE_68010);
		m68k_init();
		//note: cbs should happen after init
		m68k_set_int_ack_callback(m68k_int_cb);
		m68k_set_instr_hook_callback(m68k_trace_cb);
		m68k_set_fc_callback(m68k_fc_cb);
		m68k_pulse_reset();
		m68k_set_irq(0);
		m68k_get_context(cpuctx[i]);
	}

	m68k_set_context(cpuctx[0]);
	m68k_execute(10);
	m->size=0; //disable shadow rom
	m68k_get_context(cpuctx[0]);

	int cpu_in_reset[2]={0};

	while(1) {
		for (int i=0; i<2; i++) {
			m68k_set_context(cpuctx[i]);
			cur_cpu=i;
			if (need_raise_highest_int[i]) {
				raise_highest_int();
				need_raise_highest_int[i]=0;
			}
			if (csr_cpu_is_reset(csr, i)) {
				cpu_in_reset[i]=1;
			} else {
				if (cpu_in_reset[i]) m68k_pulse_reset();
				cpu_in_reset[i]=0;
				m68k_execute(100);
			}
			m68k_get_context(cpuctx[i]);
		}

		//ints go to job cpu
		m68k_set_context(cpuctx[0]);
		for (int i=0; i<4; i++) uart_tick(uart[i], 10);
		rtc_tick(rtc, 10);
		m68k_get_context(cpuctx[0]);
	}
	return 0;
}


