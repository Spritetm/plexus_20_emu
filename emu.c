#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <time.h>
#include "Musashi/m68k.h"
#include "uart.h"
#include "ramrom.h"
#include "csr.h"
#include "mapper.h"
#include "scsi.h"
#include "mbus.h"
#include "rtc.h"
#include "log.h"
#include "emu.h"

// Debug logging
#define EMU_LOG(msg_level, format_and_args...) \
	log_printf(LOG_SRC_EMU, msg_level, format_and_args)
#define EMU_LOG_DEBUG(format_and_args...) EMU_LOG(LOG_DEBUG, format_and_args)
#define EMU_LOG_INFO( format_and_args...) EMU_LOG(LOG_INFO,  format_and_args)

typedef struct mem_range_t mem_range_t;

typedef unsigned int (*read_cb)(void *obj, unsigned int addr);
typedef void (*write_cb)(void *obj, unsigned int addr, unsigned int val);

FILE *tracefile=NULL;
int do_tracefile=0;

int insn_id=0;

int cur_cpu;

mapper_t *mapper;
csr_t *csr;

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

int mapper_enabled=0;

void dump_cpu_state() {
	//note REG_PPC is previous PC, aka the currently executing insn
	unsigned int pc=m68k_get_reg(NULL, M68K_REG_PPC);
	unsigned int sp=m68k_get_reg(NULL, M68K_REG_SP);
	unsigned int sr=m68k_get_reg(NULL, M68K_REG_SR);

	unsigned int d0=m68k_get_reg(NULL, M68K_REG_D0);

	unsigned int mem=m68k_read_memory_8(0xc00604);

	EMU_LOG_INFO("id %d CPU %d PC %08X SP %08X SR %08X D0 %08X m %08X\n", insn_id, cur_cpu, pc, sp, sr, d0, mem);
}

void dump_callstack() {
	EMU_LOG_INFO("Callstack (CPU %d): ", cur_cpu);
	for (int i=callstack_ptr[cur_cpu]-1; i>=0; --i) {
		EMU_LOG_INFO("%06X ", callstack[cur_cpu][i]);
	}
	EMU_LOG_INFO("\n");
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
	EMU_LOG_INFO("Set up 0x%X bytes of RAM in section '%s'.\n", m->size, m->name);
}


void setup_rom(const char *name, const char *filename) {
	mem_range_t *m=find_range_by_name(name);
	assert(m);
	m->obj=rom_new(filename, m->size);
	m->read8=ram_read8;
	m->read16=ram_read16;
	m->read32=ram_read32;
	EMU_LOG_INFO("Loaded ROM '%s' into section '%s' at addr %x\n", filename, name, m->offset);
}

#define PRINT_MEMREAD 0

static unsigned int read_memory_32(unsigned int address) {
	if (address==0) EMU_LOG_DEBUG("read addr 0\n");
	mem_range_t *m=find_range_by_addr(address);
	//HACK! If this is set, diags get more verbose
	if (address==0xC00644) return 1;
	if (address==0xC006de) return 1;
	if (!m) {
		EMU_LOG_INFO("Read32 from unmapped addr %08X\n", address);
		dump_cpu_state();
		return 0xdeadbeef;
	}
	if (!m->read32) {
		EMU_LOG_INFO("No read32 implemented for '%s', addr 0x%08X\n", m->name, address);
		dump_cpu_state();
		return 0xdeadbeef;
	}
#if PRINT_MEMREAD
	EMU_LOG_INFO("read32 %s %x -> %x\n", m->name, address, m->read32(m->obj, address - m->offset));
#endif
	return m->read32(m->obj, address - m->offset);
}

static unsigned int read_memory_16(unsigned int address) {
	mem_range_t *m=find_range_by_addr(address);
	if (!m) {
		EMU_LOG_INFO("Read16 from unmapped addr %08X\n", address);
		dump_cpu_state();
		return 0xbeef;
	}
	if (!m->read16) {
		EMU_LOG_INFO("No read16 implemented for '%s', addr 0x%08X\n", m->name, address);
		dump_cpu_state();
		return 0xbeef;
	}
#if PRINT_MEMREAD
	EMU_LOG_INFO("read16 %s %x -> %x\n", m->name, address, m->read16(m->obj, address - m->offset));
#endif
	return m->read16(m->obj, address - m->offset);
}

static unsigned int read_memory_8(unsigned int address) {
	mem_range_t *m=find_range_by_addr(address);
	if (!m) {
		EMU_LOG_INFO("Read8 from unmapped addr %08X\n", address);
		dump_cpu_state();
		return 0x5a;
	}
	if (!m->read8) {
		EMU_LOG_INFO("No read8 implemented for '%s', addr 0x%08X\n", m->name, address);
		dump_cpu_state();
		return 0x5a;
	}
#if PRINT_MEMREAD
	EMU_LOG_INFO("read8 %s %x -> %x\n", m->name, address, m->read8(m->obj, address - m->offset));
#endif
	return m->read8(m->obj, address - m->offset);
}


static void write_memory_8(unsigned int address, unsigned int value) {
	watch_write(address, value, 8);
	mem_range_t *m=find_range_by_addr(address);
	if (!m) {
		EMU_LOG_INFO("Write8 to unmapped addr %08X data 0x%X\n", address, value);
		return;
	}
	if (!m->write8) {
		EMU_LOG_INFO("No write8 implementation for %s, addr 0x%08X, data 0x%X\n", m->name, address, value);
		return;
	}
	m->write8(m->obj, address - m->offset, value);
}

static void write_memory_16(unsigned int address, unsigned int value) {
	watch_write(address, value, 16);
	mem_range_t *m=find_range_by_addr(address);
	if (!m) {
		EMU_LOG_INFO("Write16 to unmapped addr %08X data 0x%X\n", address, value);
		dump_cpu_state();
		return;
	}
	if (!m->write16) {
		EMU_LOG_INFO("No write16 implementation for %s, addr 0x%08X, data 0x%X\n", m->name, address, value);
		dump_cpu_state();
		return;
	}
	m->write16(m->obj, address - m->offset, value);
}

static void write_memory_32(unsigned int address, unsigned int value) {
	watch_write(address, value, 32);
	mem_range_t *m=find_range_by_addr(address);
	if (!m) {
		EMU_LOG_INFO("Write32 to unmapped addr %08X data 0x%X\n", address, value);
		dump_cpu_state();
		return;
	}
	if (!m->write32) {
		EMU_LOG_INFO("No write32 implementation for '%s', addr 0x%08X, data 0x%X\n", m->name, address, value);
		dump_cpu_state();
		return;
	}
	m->write32(m->obj, address - m->offset, value);
}

unsigned int fc_bits=0;

void emu_set_cur_mapid(uint8_t id) {
	mapper_set_mapid(mapper, id);
}

//Note: This should be reworked. Better to have generic [read/write]_memory_[8|16|32]
//functions that take flags, then dispatch to memory handlers that also take those flags.
//The flags contain access type and device that accesses it (job, dma, scsi, mbus) and
//the memory handlers can check against permissions and throw a bus error by calling
//some function with the flags. That function can then decode the source and do the right
//thing to actually throw the error.

static int check_mem_access(unsigned int address, int flags) {
	if (!mapper_enabled) return 1;
	if ((fc_bits&3)==2) flags=ACCESS_X;
	if (fc_bits&4) flags|=ACCESS_SYSTEM;
	if (!mapper_access_allowed(mapper, address, flags)) {
		EMU_LOG_DEBUG("Bus error! Access %x\n", address);
//		if (address==0x1000) do_tracefile=1;
//		dump_cpu_state();
//		dump_callstack();
		cst_set_access_error(csr, cur_cpu, ACCESS_ERROR_A);
		m68k_pulse_bus_error();
		return 0;
	}
	return 1;
}

int force_a23=3; //start with both forced to boot from rom

void emu_set_force_a23(int val) {
	force_a23=val;
}

unsigned int m68k_read_memory_32(unsigned int address) {
	if (force_a23 & (1<<cur_cpu)) address|=0x800000;
	if (!check_mem_access(address, ACCESS_R)) return 0;
	return read_memory_32(address);
}

unsigned int m68k_read_memory_16(unsigned int address) {
	if (force_a23 & (1<<cur_cpu)) address|=0x800000;
	if (!check_mem_access(address, ACCESS_R)) return 0;
	return read_memory_16(address);
}


unsigned int m68k_read_memory_8(unsigned int address) {
	if (force_a23 & (1<<cur_cpu)) address|=0x800000;
	if (!check_mem_access(address, ACCESS_R)) return 0;
	return read_memory_8(address);
}

void m68k_write_memory_8(unsigned int address, unsigned int value) {
	if (force_a23 & (1<<cur_cpu)) address|=0x800000;
	if (!check_mem_access(address, ACCESS_W)) return;
	write_memory_8(address, value);
}

void m68k_write_memory_16(unsigned int address, unsigned int value) {
	if (force_a23 & (1<<cur_cpu)) address|=0x800000;
	if (!check_mem_access(address, ACCESS_W)) return;
	write_memory_16(address, value);
}

void m68k_write_memory_32(unsigned int address, unsigned int value) {
	if (force_a23 & (1<<cur_cpu)) address|=0x800000;
	if (!check_mem_access(address, ACCESS_W)) return;
	write_memory_32(address, value);
}



//Used for SCSI DMA transfers as well as mbus transfers.
int emu_read_byte(int addr) {
	int access_flags=ACCESS_R|ACCESS_SYSTEM;
	if (!mapper_access_allowed(mapper, addr, access_flags)) return -1;
	return read_memory_8(addr);
}

int emu_write_byte(int addr, int val) {
	int access_flags=ACCESS_R|ACCESS_SYSTEM;
	if (!mapper_access_allowed(mapper, addr, access_flags)) return 0;
	write_memory_8(addr, val);
	return -1;
}

void emu_mbus_error(unsigned int addr) {
	mem_range_t *r=find_range_by_name("CSR");
	csr_t *c=(csr_t*)r->obj;
	csr_raise_error(c, CSR_ERR_MBUS, addr);
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
	mapper_enabled=do_enable;
	mem_range_t *r=find_range_by_name("RAM");
	mem_range_t *mr=find_range_by_name("MAPRAM");
	if (do_enable) {
		if (r->size!=0) {
			mr->size=r->size;
			r->size=0;
			EMU_LOG_DEBUG("Mapper ENABLED\n");
		}
	} else {
		if (mr->size!=0) {
			r->size=mr->size;
			mr->size=0;
			EMU_LOG_DEBUG("Mapper DISABLED\n");
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


//1 if held
int emu_try_mbus_held() {
	mem_range_t *r=find_range_by_name("CSR");
	csr_t *c=(csr_t*)r->obj;
	return csr_try_mbus_held(c);
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


void m68k_fc_cb(unsigned int fc) {
	fc_bits=fc;
	mapper_set_sysmode(mapper, fc&4);
	//Guess: mapid resets on int?
	if ((fc&0x7)==7) {
		mapper_set_mapid(mapper, 0);
	}
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
	EMU_LOG_DEBUG("Int ack %x\n", r);
	return r;
}

int need_raise_highest_int[2]={0};

void emu_raise_int(uint8_t vector, uint8_t level, int cpu) {
	EMU_LOG_DEBUG("Interrupt raised: %x\n", vector);
	vectors[cpu][vector]=level;
	need_raise_highest_int[cpu]=1;
}

void emu_raise_rtc_int() {
	mem_range_t *r=find_range_by_name("CSR");
	csr_t *c=(csr_t*)r->obj;
	if (csr_get_rtc_int_ena(c, 0)) emu_raise_int(0x83, 6, 0);
	if (csr_get_rtc_int_ena(c, 1)) emu_raise_int(0x83, 6, 1);
}

int old_val;

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
	unsigned int sr=m68k_get_reg(NULL, M68K_REG_SR);
	if (do_tracefile) fprintf(tracefile, "%d %d %06x %x\n", insn_id, cur_cpu, pc, sr);
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
	EMU_LOG_INFO("At ^^: Watch addr %06X changed to %08X\n", addr, val);
}

int emu_get_cur_cpu() {
	return cur_cpu;
}

void emu_bus_error() {
	m68k_pulse_bus_error();
}

void emu_start(emu_cfg_t *cfg) {
	tracefile=fopen("trace.txt","w");
	setup_ram("RAM");
	setup_ram("SRAM");
	setup_ram("RTC_RAM");
	setup_rom("U15", cfg->u15_rom); //used to be U17
	setup_rom("U17", cfg->u17_rom); //used to be U19
	uart_t *uart[4];
	uart[0]=setup_uart("UART_A", 1);
	uart[1]=setup_uart("UART_B", 0);
	uart[2]=setup_uart("UART_C", 0);
	uart[3]=setup_uart("UART_D", 0);
	setup_scsi("SCSIBUF");
	csr=setup_csr("CSR", "MMIO_WR", "SCSIBUF");
	mapper=setup_mapper("MAPPER", "MAPRAM", "RAM");
	setup_nop("MBUSIO");
	setup_mbus("MBUSMEM");
	rtc_t *rtc=setup_rtc("RTC");

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

	int cpu_in_reset[2]={0};
	int cycles_remaining[2]={0};

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
				m68k_execute(100+cycles_remaining[i]);
				cycles_remaining[i]=m68k_cycles_remaining();
			}
			m68k_get_context(cpuctx[i]);
		}

		//ints go to job cpu
		m68k_set_context(cpuctx[0]);
		for (int i=0; i<4; i++) uart_tick(uart[i], 10);
		rtc_tick(rtc, 10);
		m68k_get_context(cpuctx[0]);
	}
}


