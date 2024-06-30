/*
 Top-level Plexus P/20 emulation logic.
*/

/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <jeroen@spritesmods.com> wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return. - Sprite_tm
 * ----------------------------------------------------------------------------
 */


#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <time.h>
#include <signal.h>
#include <sys/time.h>
#include <unistd.h>
#ifdef __EMSCRIPTEN__
#include "emscripten.h"
#endif
#include "Musashi/m68k.h"
#include "uart.h"
#include "ramrom.h"
#include "csr.h"
#include "mapper.h"
#include "scsi.h"
#include "scsi_dev_hd.h"
#include "mbus.h"
#include "rtc.h"
#include "rtcram.h"
#include "log.h"
#include "emu.h"
#include "int.h"
#include "sysvr2-strace.h"

//If this is set to 1, you can set the variable do_tracefile to a value
//of (1<<cpu) to print out one line indicating the PC and other info
//for each instruction executed to a file called 'trace.txt'.
#define SUPPORT_TRACEFILE 0

//if running realtime, we'll sleep for a bit every SLEEP_EVERY_US us
#define SLEEP_EVERY_US 10000 //10ms = 100Hz

//We run dma for this long, then job for this long, then service the
//peripherals.
#define CPU_RUN_US 10

//CPU speed in Hz
#define CPU_SPEED_HZ 10000000 //10MHz


// Debug logging
#define EMU_LOG(msg_level, format_and_args...) \
	log_printf(LOG_SRC_EMU, msg_level, format_and_args)
#define EMU_LOG_DEBUG(format_and_args...) EMU_LOG(LOG_DEBUG, format_and_args)
#define EMU_LOG_INFO( format_and_args...) EMU_LOG(LOG_INFO,  format_and_args)

typedef struct mem_range_t mem_range_t;

typedef unsigned int (*read_cb)(void *obj, unsigned int addr);
typedef void (*write_cb)(void *obj, unsigned int addr, unsigned int val);

#if SUPPORT_TRACEFILE
FILE *tracefile=NULL;
int do_tracefile=0;
#endif

//Instruction ID. Increases with every CPU instruction executed. You can use
//this to correlate a trace file and dump_cpu_state output.
unsigned int insn_id=0;

//Currently emulated CPU. 0=dma, 1=job
int cur_cpu=0;
//Bits output by the 68000 on the FC pins.
unsigned int fc_bits=0;
//If true, this dumps CPU state after every instruction.
int trace_enabled=0;
//If true, the mapper is currently enabled ("MAPRAM" region is non-zero).
//If false, the mapper is disabled ("RAM" region is nonzero).
int mapper_enabled=0;
//This contains a bitmask for the DMA CPU (1<<0) and JOB CPU (1<<1). If the
//bit is true, the A23 line is forced high for memory accesses. This mainly results
//in the ROMs U17/U15 being mirrored to address 0.
int force_a23=3; //start with both forced to boot from rom
//If non-zero, this writes a byte with a parity error whenever a byte is written.
//Bitmask: (1<<0) writes parity errors for low bytes, (1<<1) for high bytes.
int parity_force_error=0;
//Set if the CSR for multibus diag loopback is enabled.
int mbus_diag_en=0;

mapper_t *mapper;
csr_t *csr;

#define CALLSTACK_SZ 1024
int32_t callstack[2][CALLSTACK_SZ];
int callstack_ptr[2]={0};

//Defines a memory range.
struct mem_range_t {
	const char *name;		//Name of the range
	uint32_t offset;		//Offset in the CPU memory map
	uint32_t size;			//Size of the range (may not be the size of the backing memory)
	int flags;				//Flags. One of FLAG_*
	void *obj;				//Opaque object associated with the range
	read_cb read8;			//
	read_cb read16;			// Read/write functions for the range. Note that these
	read_cb read32;			// get passed the address within the range rather than
	write_cb write8;		// within the CPU address space.
	write_cb write16;		//
	write_cb write32;		//
};

//Flags for mem_range_t->flags
#define FLAG_USR_OK 1 //Memory can be accessed by user mode on job cpu

static mem_range_t memory[]={
//	{.name="RAM",     .offset=0, .size=0x200000, .flags=FLAG_USR_OK}, //only 2MiB of RAM
	{.name="RAM",     .offset=0, .size=0x800000, .flags=FLAG_USR_OK}, //fully decked out with 8MiB of RAM
	{.name="MAPRAM",  .offset=0, .size=0, .flags=FLAG_USR_OK}, //MMU-mapped RAM
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


void dump_cpu_state() {
	//note REG_PPC is previous PC, aka the currently executing insn
	unsigned int pc=m68k_get_reg(NULL, M68K_REG_PPC);
	unsigned int sp=m68k_get_reg(NULL, M68K_REG_SP);
	unsigned int sr=m68k_get_reg(NULL, M68K_REG_SR);
	unsigned int d0=m68k_get_reg(NULL, M68K_REG_D0);

	EMU_LOG_INFO("id %d CPU %d PC %08X SP %08X SR %08X D0 %08X\n", insn_id, cur_cpu, pc, sp, sr, d0);
}

void dump_cpu_state_all() {
	int regs[]={
		M68K_REG_PC, M68K_REG_SR, M68K_REG_SP, M68K_REG_USP, 
		M68K_REG_D0, M68K_REG_D1, M68K_REG_D2, M68K_REG_D3, 
		M68K_REG_D4, M68K_REG_D5, M68K_REG_D6, M68K_REG_D7, 
		M68K_REG_A0, M68K_REG_A1, M68K_REG_A2, M68K_REG_A3, 
		M68K_REG_A4, M68K_REG_A5, M68K_REG_A6, M68K_REG_A7, 
	};
	char *regnames[]={
		"PC", "SR", "SP", "USP", 
		"D0", "D1", "D2", "D3",
		"D4", "D5", "D6", "D7",
		"A0", "A1", "A2", "A3",
		"A4", "A5", "A6", "A7"
	};
	EMU_LOG_INFO("CPU %d STATE DUMP:\n", cur_cpu);
	for (int i=0; i<sizeof(regs)/sizeof(regs[0]); i++) {
		EMU_LOG_INFO("%s\t%08X%s", regnames[i], m68k_get_reg(NULL, regs[i]), ((i&3)==3)?"\n":"\t");
	}
	int pc=m68k_get_reg(NULL, M68K_REG_PC);
	int pos=pc-18;
	char buf[1024];
	//try to align to full insn
	pos+=m68k_disassemble(buf, pos, M68K_CPU_TYPE_68010);
	while (pos<pc+16) {
		pos+=m68k_disassemble(buf, pos, M68K_CPU_TYPE_68010);
		EMU_LOG_INFO("%06X %s%s\n", pos, buf, pos==pc?"\t\t<-- PC":"");
	}
	EMU_LOG_INFO("END DUMP\n");
}


void dump_callstack() {
	EMU_LOG_INFO("Callstack (CPU %d): ", cur_cpu);
	for (int i=callstack_ptr[cur_cpu]-1; i>=0; --i) {
		EMU_LOG_INFO("%06X ", callstack[cur_cpu][i]);
	}
	EMU_LOG_INFO("\n");
}


static void watch_write(unsigned int addr, unsigned int val, int len);

//Finds a memory range given a name.
static mem_range_t *find_range_by_name(const char *name) {
	int i=0;
	while (memory[i].name!=NULL) {
		if (strcmp(memory[i].name, name)==0) return &memory[i];
		i++;
	}
	return NULL;
}

//Find a range given an address that falls in that range.
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

#if 0
//No-op read
static unsigned int nop_read(void *obj, unsigned int a) {
	return 0;
}
#endif

//No-op write
static void nop_write(void *obj, unsigned int a, unsigned int val) {
}

//Set up a range containing RAM. The RAM can be smaller than the size of the memory
//region, in which case accesses 'wrap around'.
//Note: if size_bytes is negative, the size of the full region is used.
void setup_ram(const char *name, int size_bytes) {
	mem_range_t *m=find_range_by_name(name);
	assert(m);
	if (size_bytes<0) size_bytes=m->size;
	m->obj=ram_new(size_bytes);
	m->read8=ram_read8;
	m->read16=ram_read16;
	m->read32=ram_read32;
	m->write8=ram_write8;
	m->write16=ram_write16;
	m->write32=ram_write32;
	EMU_LOG_INFO("Set up 0x%X bytes of RAM in section '%s'.\n", m->size, m->name);
}

//Set up a range containing read-only memory. The contents are read from
//the given file.
void setup_rom(const char *name, const char *filename) {
	mem_range_t *m=find_range_by_name(name);
	assert(m);
	m->obj=rom_new(filename, m->size);
	m->read8=ram_read8;
	m->read16=ram_read16;
	m->read32=ram_read32;
	m->write8=nop_write;
	m->write16=nop_write;
	m->write32=nop_write;
	EMU_LOG_INFO("Loaded ROM '%s' into section '%s' at addr %x\n", filename, name, m->offset);
}

//Set up a range for an UART.
uart_t *setup_uart(const char *name, int is_console) {
	mem_range_t *m=find_range_by_name(name);
	uart_t *u=uart_new(name, is_console);
	m->obj=u;
	m->write8=uart_write8;
	m->read8=uart_read8;
	return u;
}

//Set up the SCSI buffer range.
scsi_t *setup_scsi(const char *name) {
	mem_range_t *m=find_range_by_name(name);
	scsi_t *r=scsi_new();
	m->obj=r;
	m->write8=scsi_write8;
	m->write16=scsi_write16;
	m->read16=scsi_read16;
	m->read8=scsi_read8;
	return r;
}

//Set up the range the time/date registers of the RTC are in.
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

// RTC RAM is physically part of the RTC, but implemented in a different
// virtual device for simplicity; it is like standard RAM, but persistent
// over separate emulator runs.
void setup_rtcram(const char *name, const char *filename) {
	mem_range_t *m=find_range_by_name(name);
	m->obj=rtcram_new(filename);
	m->write8=rtcram_write8;
	m->write16=rtcram_write16;
	m->write32=rtcram_write32;
	m->read8=rtcram_read8;
	m->read16=rtcram_read16;
	m->read32=rtcram_read32;
	EMU_LOG_INFO("Set up 0x%X bytes of persistent RAM in section '%s'.\n", m->size, m->name);
}

//Set up Control/Status Register (CSR) range
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
	mm->read16=csr_read16_mmio;
	return r;
}

//Set up mapper range.
mapper_t *setup_mapper(const char *name, const char *mapram, const char *physram, int yolo) {
	mem_range_t *m=find_range_by_name(name);
	mem_range_t *mr=find_range_by_name(mapram);
	mem_range_t *pr=find_range_by_name(physram);

	mapper_t *map=mapper_new(pr->obj, pr->size, yolo);
	m->obj=map;
	m->write32=mapper_write32;
	m->write16=mapper_write16;
	m->write8=mapper_write8;
	m->read32=mapper_read32;
	m->read16=mapper_read16;
	m->read8=mapper_read8;

	mr->obj=map;
	mr->read8=mapper_ram_read8;
	mr->read16=mapper_ram_read16;
	mr->read32=mapper_ram_read32;
	mr->write8=mapper_ram_write8;
	mr->write16=mapper_ram_write16;
	mr->write32=mapper_ram_write32;
	return map;
}


//Set up Multibus ranges
static void setup_mbus(const char *name, const char *ioname) {
	mem_range_t *m=find_range_by_name(name);
	mem_range_t *io=find_range_by_name(ioname);
	//note mbus needs no obj
	m->read8=mbus_read8;
	m->read16=mbus_read16;
	m->read32=mbus_read32;
	m->write8=mbus_write8;
	m->write16=mbus_write16;
	m->write32=mbus_write32;
	io->read8=mbus_io_read;
	io->read16=mbus_io_read;
	io->read32=mbus_io_read;
	io->write8=mbus_io_write;
	io->write16=mbus_io_write;
	io->write32=mbus_io_write;
}


#if 0
//Set up a range where writes don't do anything and reads always return 0.
static void setup_nop(const char *name) {
	mem_range_t *m=find_range_by_name(name);
	m->write8=nop_write;
	m->write16=nop_write;
	m->write32=nop_write;
	m->read8=nop_read;
	m->read16=nop_read;
	m->read32=nop_read;
}
#endif

//Check if the current CPU can access the given memory range. Note that this does
//not do mapper permission checks: it only checks if the range itself is accessible.
//Also throws a bust error if not accessible.
static int check_can_access(mem_range_t *m, unsigned int address) {
	int ret=1;
	if (cur_cpu==1 && ((fc_bits&4)==0) && ((m->flags&FLAG_USR_OK)==0)) {
		EMU_LOG_INFO("Faulting CPU %d for accessing non-RAM address %X in range %s in user mode (fc=%x)\n", cur_cpu, address, m->name, fc_bits);
		csr_set_access_error(csr, cur_cpu, ACCESS_ERROR_AJOB, address, 0);
		dump_cpu_state();
		dump_callstack();
		ret=0;
	}
	if (!ret) {
		csr_set_access_error(csr, cur_cpu, ACCESS_ERROR_A, address, 0);
		m68k_pulse_bus_error(); //note this function longjmp()s and never returns.
	}
	return ret;
}


static unsigned int read_memory_32(unsigned int address) {
	mem_range_t *m=find_range_by_addr(address);
	//HACK! If this is enabled, diags get more verbose
#if 0
	if (address==0xC00644) return 1; //output 'expected' diag lines
	if (address==0xC006de) return 1; //output PC of next subtest
#endif
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

	if (!check_can_access(m, address)) return 0;
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
	if (!check_can_access(m, address)) return 0;
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
	if (!check_can_access(m, address)) return 0;
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
	if (!check_can_access(m, address)) return;
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
	if (!check_can_access(m, address)) return;
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
	if (!check_can_access(m, address)) return;
	m->write32(m->obj, address - m->offset, value);
}

void emu_set_cur_mapid(uint8_t id) {
	mapper_set_mapid(mapper, id);
}

//Note: This should be reworked. Better to have generic [read/write]_memory_[8|16|32]
//functions that take flags, then dispatch to memory handlers that also take those flags.
//The flags contain access type and device that accesses it (job, dma, scsi, mbus) and
//the memory handlers can check against permissions and throw a bus error by calling
//some function with the flags. That function can then decode the source and do the right
//thing to actually throw the error.

//Check if the mapper allows a memory access from the current CPU (note this is not 
//used for MBUS or SCSI DMA) to a certain address.
//Returns true for access, false for no access
//Note that this also generates a bus error on the current CPU if the access was denied.
static int check_mem_access(unsigned int address, int flags) {
	if (!mapper_enabled) return 1;
	if ((fc_bits&3)==2) flags=ACCESS_X;
	if (fc_bits&4) flags|=ACCESS_SYSTEM;
	int access=mapper_access_allowed(mapper, address, flags);
	if (access!=ACCESS_ERROR_OK) {
		if (log_level_active(LOG_SRC_MAPPER, LOG_DEBUG)) {
			EMU_LOG_INFO("Illegal access! Access %x. Generating bus error.\n", address);
			dump_cpu_state();
			dump_callstack();
		}
		csr_set_access_error(csr, cur_cpu, access, address, flags&ACCESS_W);

		//note: THIS FUNCTION WILL NOT RETURN!
		//(m68k_pulse_bus error calls m68ki_exception_bus_error, which
		//ends with a longjmp.)
		m68k_pulse_bus_error();
		return 0; //never reached
	}
	return 1;
}

void emu_set_force_a23(int val) {
	force_a23=val;
}

void emu_set_force_parity_error(int val) {
	if (parity_force_error!=val) EMU_LOG_DEBUG("Parity error force %x enabled\n", val);
	parity_force_error=val;
}


/*
Parity error handling.
The diags can enable writing bytes/words to memory with an invalid parity bit. We don't
have parity bits in the emulator. Rather, we have a buffer containing which memory addresses
currently have parity errors. A write will set an entry there if parity errors are enabled
(or clear it if one is there and parity errors are disabled) and a read will raise a
parity error interrupt if the buffer contains the address.
*/

#define PARITY_ERR_BUF_SZ 8
#define PARITY_ERR_ACTIVE 0x80000000
static unsigned int parity_errors[PARITY_ERR_BUF_SZ]={0};
static unsigned int parity_errors_count=0;

void check_parity_error(unsigned int address, int len) {
	if (parity_errors_count==0) return;
	if (address>=0x80000) return; //no parity errors outside of RAM
	int v=0;
	for (int a=address; a<address+len; a++) {
		for (int i=0; i<PARITY_ERR_BUF_SZ; i++) {
			if (parity_errors[i]==(a|PARITY_ERR_ACTIVE)) {
				if (a&1) v|=PARITY_ERROR_L; else v|=PARITY_ERROR_H;
			}
		}
	}
	if (v) {
		EMU_LOG_DEBUG("Raising parity error on addr %x\n", address);
		emu_raise_int(INT_VECT_PARITY_ERR, INT_LEVEL_PARITY_ERR, cur_cpu);
		csr_set_parity_error(csr, v);
	}
}


static void handle_write_parity_error(unsigned int address, int len) {
	if (parity_errors_count==0 && parity_force_error==0) return;
	if (address>=0x80000) return; //no parity errors outside of RAM
	for (int a=address; a<address+len; a++) {
		if (((!(a&1)) && (parity_force_error&1)) ||
				((a&1) && (parity_force_error&2))) {
			//Mark as error
			EMU_LOG_DEBUG("Marking parity error on addr %x\n", a);
			for (int i=0; i<PARITY_ERR_BUF_SZ; i++) {
				if (parity_errors[i]==(a|PARITY_ERR_ACTIVE)) break;
				if (!(parity_errors[i]&PARITY_ERR_ACTIVE)) {
					parity_errors[i]=a|PARITY_ERR_ACTIVE;
					parity_errors_count++;
					break;
				}
			}
		} else {
			//Clear error
			for (int i=0; i<PARITY_ERR_BUF_SZ; i++) {
				if (parity_errors[i]==(a|PARITY_ERR_ACTIVE)) {
				EMU_LOG_DEBUG("Clearing parity error on addr %x\n", a);
					parity_errors[i]=0;
					parity_errors_count--;
				}
			}
		}
	}
}

unsigned int m68k_read_memory_32(unsigned int address) {
	if (force_a23 & (1<<cur_cpu)) address|=0x800000;
	if (!check_mem_access(address, ACCESS_R)) return 0;
	check_parity_error(address, 4);
	return read_memory_32(address);
}

unsigned int m68k_read_memory_16(unsigned int address) {
	if (force_a23 & (1<<cur_cpu)) address|=0x800000;
	if (!check_mem_access(address, ACCESS_R)) return 0;
	check_parity_error(address, 2);
	return read_memory_16(address);
}


unsigned int m68k_read_memory_8(unsigned int address) {
	if (force_a23 & (1<<cur_cpu)) address|=0x800000;
	if (!check_mem_access(address, ACCESS_R)) return 0;
	check_parity_error(address, 1);
	return read_memory_8(address);
}

void m68k_write_memory_8(unsigned int address, unsigned int value) {
	if (force_a23 & (1<<cur_cpu)) address|=0x800000;
	if (!check_mem_access(address, ACCESS_W)) return;
	handle_write_parity_error(address, 1);
	write_memory_8(address, value);
}

void m68k_write_memory_16(unsigned int address, unsigned int value) {
	if (force_a23 & (1<<cur_cpu)) address|=0x800000;
	if (!check_mem_access(address, ACCESS_W)) return;
	handle_write_parity_error(address, 2);
	write_memory_16(address, value);
}

void m68k_write_memory_32(unsigned int address, unsigned int value) {
	if (force_a23 & (1<<cur_cpu)) address|=0x800000;
	if (!check_mem_access(address, ACCESS_W)) return;
	handle_write_parity_error(address, 4);
	write_memory_32(address, value);
}


//Used for SCSI DMA transfers as well as mbus transfers.
int emu_read_byte(int addr) {
	int access_flags=ACCESS_R|ACCESS_SYSTEM;
	if (mapper_access_allowed(mapper, addr, access_flags)!=ACCESS_ERROR_OK) {
		return -1;
	}
	return read_memory_8(addr);
}

//Used for SCSI DMA transfers as well as mbus transfers.
int emu_write_byte(int addr, int val) {
	int access_flags=ACCESS_W|ACCESS_SYSTEM;
	if (mapper_access_allowed(mapper, addr, access_flags)!=ACCESS_ERROR_OK) return -1;
	write_memory_8(addr, val);
	return 0;
}


void emu_mbus_error(unsigned int addr) {
	csr_set_access_error(csr, 1, ACCESS_ERROR_MBTO, addr&0xffffff, !(addr&EMU_MBUS_ERROR_READ));
	if (mbus_diag_en && (!(addr&EMU_MBUS_ERROR_READ))) {
		emu_raise_int(INT_VECT_MB_IF_ERR, INT_LEVEL_MB_IF_ERR, 1);
	}
	if (addr&EMU_MBUS_BUSERROR) {
		emu_bus_error();
	}
}

void emu_set_mb_diag(int ena) {
	if (mbus_diag_en!=ena) EMU_LOG_DEBUG("MB DIAG %d\n", ena);
	mbus_diag_en=ena;
}

int emu_get_mb_diag() {
	return mbus_diag_en;
}

//true if mbus is held
int emu_try_mbus_held() {
	return csr_try_mbus_held(csr);
}


/*
 This is called to enable/disable the mapper.
 The way this works is that there are two regions at address 0: RAM and MAPRAM.
 Only one of the two has a non-zero size at all sizes, and if the mapper is
 turned on/off, the non-zero and zero sizes are swapped.
*/
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

void m68k_fc_cb(unsigned int fc) {
	fc_bits=fc;
	mapper_set_sysmode(mapper, fc&4);
}

uint32_t stget32(void *ctx, uint32_t addr)
{
	return read_memory_32(addr);
}

uint8_t stget8(void *ctx, uint32_t addr)
{
	return read_memory_8(addr);
}

//note: only invoked if cfg->tracesyscalls is true
void m68k_trap_cb(unsigned int vector) {
	if (cur_cpu != 1 || vector != 32) {
		return;
	}
	unsigned int d0 = m68k_get_reg(NULL, M68K_REG_D0);
	unsigned int sp = m68k_get_reg(NULL, M68K_REG_A7);
	log_printf(LOG_SRC_STRACE, LOG_INFO, "strace: %s\n", m68k_strace(NULL, d0, sp));
}

//has a level if triggered, otherwise 0
uint8_t vectors[2][256]={0};

//Sets the current interrupt level to whatever the highest level
//amongst the active vectors is.
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

//Interrupt acknowledge
int m68k_int_cb(int level) {
	int r=0xf; //if nothing is found, return the 'unset interrupt' exception.
	//find an active vector for this level
	for (int i=0x10; i<256; i++) {
		if (vectors[cur_cpu][i]==level) {
			r=i;
		}
	}
	if (level==INT_LEVEL_UART) {
		//Not sure what clears an UART interrupt. As a workaround,
		//we clear it here, that seems to work.
		vectors[cur_cpu][r]=0;
	}
	raise_highest_int();
	//we ignore the clock ints when printing debug messages because it spams
	//the console too much.
	if (r!=INT_VECT_CLOCK) {
		EMU_LOG_DEBUG("Int ack level %x cpu %x vect %x\n", level, cur_cpu, r);
	}
	return r;
}

int need_raise_highest_int[2]={0};

void emu_raise_int(uint8_t vector, uint8_t level, int cpu) {
	if (vectors[cpu][vector]!=level) {
		//we again ignore the clock ints when printing debug messages
		if (vector!=INT_VECT_CLOCK) {
			EMU_LOG_DEBUG("Interrupt %s: %x\n", level?"raised":"cleared", vector);
		}
		vectors[cpu][vector]=level;
		need_raise_highest_int[cpu]=1;
		//cut timeslice short because we possibly need to handle peripherals or the
		//other CPU.
		m68k_end_timeslice();
	}
}

void emu_raise_rtc_int() {
	mem_range_t *r=find_range_by_name("CSR");
	csr_t *c=(csr_t*)r->obj;
	if (csr_get_rtc_int_ena(c, 0)) emu_raise_int(INT_VECT_CLOCK, INT_LEVEL_CLOCK, 0);
	if (csr_get_rtc_int_ena(c, 1)) emu_raise_int(INT_VECT_CLOCK, INT_LEVEL_CLOCK, 1);
}

//It's easy for the callstack handler to get confused. Make sure there's never
//too few or too many items on here.
void handle_callstack_ovf_udf(int cpu) {
	if (callstack_ptr[cpu]<0) callstack_ptr[cpu]=0;
	if (callstack_ptr[cpu]>=CALLSTACK_SZ) {
		//We overflowed. Assume the first half of the callstack is
		//crud, move the second half there and we have half a callstack
		//free again.
		for (int i=0; i<CALLSTACK_SZ/2; i++) {
			callstack[cpu][i]=callstack[cpu][i+CALLSTACK_SZ/2];
		}
		callstack_ptr[cpu]=CALLSTACK_SZ/2;
	}
}

void m68k_trace_cb(unsigned int pc) {
	static unsigned int prev_pc=0;
	insn_id++;
#if 0
	//Example of how to set the equivalent of a breakpoint (dump CPU state 
	//when a certain PC is reached)
	if (pc==0x33d6) {
		EMU_LOG_INFO("trace: write\n");
		dump_cpu_state_all();
		dump_callstack();
	}
#endif

	//note: pc already is advanced to the next insn when this is called
	//but ir is not
	unsigned int ir=m68k_get_reg(NULL, M68K_REG_IR);
	//decode jsr/trs instructions for callstack tracing
	//("Mom, can we have backtrace support?" "We have backtrace support 
	//at home!" Backtrace support at home: )
	if ((ir&0xFFC0)==0x4e80) callstack[cur_cpu][callstack_ptr[cur_cpu]++]=prev_pc;
	if (ir==0x4E75) callstack_ptr[cur_cpu]--;
	handle_callstack_ovf_udf(cur_cpu);

	prev_pc=pc;

#if SUPPORT_TRACEFILE
	unsigned int sr=m68k_get_reg(NULL, M68K_REG_SR);
	if (do_tracefile&(1<<cur_cpu)) fprintf(tracefile, "%d %d %06x %x\n", insn_id, cur_cpu, pc, sr);
#endif
	if (trace_enabled) {
		dump_cpu_state();
	}
}


static void watch_write(unsigned int addr, unsigned int val, int len) {
#if 0
	//An example on how you can watch a write from the CPUs to a certain address.
	if (addr/4==0xC02de2/4) {
		dump_callstack();
	} else {
		return;
	}
	dump_cpu_state();
	EMU_LOG_INFO("At ^^: Watch addr %06X changed to %08X\n", addr, val);
#else
	return;
#endif
}

int emu_get_cur_cpu() {
	return cur_cpu;
}

void emu_bus_error() {
	EMU_LOG_INFO("Bus error on CPU %d\n", cur_cpu);
	dump_cpu_state();
	m68k_pulse_bus_error(); //note this function longjmp()s and never returns
}

void emu_schedule_int_us(int us) {
	int cycles=us*10; //cpu is 10MHz
	int rem=m68k_cycles_remaining();
	if (rem>cycles) {
		m68k_modify_timeslice(cycles);
	}
}

int dump_status=0;

//Signal handler for ctrl+\.
static void sig_hdl(int sig) {
	dump_status=1;
	m68k_modify_timeslice(0);
}


void emu_start(emu_cfg_t *cfg) {
#if SUPPORT_TRACEFILE
	tracefile=fopen("trace.txt","w");
#endif
//Note: this is a bitmask for which CPU gets logged. (1<<0) for dma, (1<<1) for job cpu.
//	do_tracefile=(1<<1);
	setup_ram("RAM", cfg->mem_size_bytes);
	setup_ram("SRAM", -1);
	setup_rtcram("RTC_RAM", cfg->rtcram);
	setup_rom("U15", cfg->u15_rom); //used to be U17
	setup_rom("U17", cfg->u17_rom); //used to be U19
	uart_t *uart[4];
	uart[0]=setup_uart("UART_A", 1);
	uart[1]=setup_uart("UART_B", 0);
	uart[2]=setup_uart("UART_C", 0);
	uart[3]=setup_uart("UART_D", 0);
	scsi_t *scsi=setup_scsi("SCSIBUF");
	scsi_dev_t *hd1=scsi_dev_hd_new(cfg->hd0img, cfg->cow_dir);
	scsi_add_dev(scsi, hd1, 0);
	csr=setup_csr("CSR", "MMIO_WR", "SCSIBUF");
	mapper=setup_mapper("MAPPER", "MAPRAM", "RAM", !cfg->noyolo);
	setup_mbus("MBUSMEM", "MBUSIO");
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
		if (cfg->tracesyscalls) m68k_set_trap_instr_callback(m68k_trap_cb);
		m68k_pulse_reset();
		m68k_set_irq(0);
		m68k_get_context(cpuctx[i]);
	}
	signal(SIGQUIT, sig_hdl); // ctrl+\ to dump status

	int cpu_in_reset[2]={0};
	int cycles_remaining[2]={0};

	struct timeval last_delay_at;
	int emulated_us_since_last_delay=0;
	gettimeofday(&last_delay_at, NULL);

	while(1) {
		for (int i=0; i<2; i++) {
			m68k_set_context(cpuctx[i]);
			cur_cpu=i;
			if (need_raise_highest_int[i]) {
				raise_highest_int();
				need_raise_highest_int[i]=0;
			}
			if (csr_cpu_is_reset(csr, i)) {
				//Mark CPU as in reset and don't execute code on it.
				cpu_in_reset[i]=1;
			} else {
				if (cpu_in_reset[i]) {
					//CPU went from reset to enabled. Pulse reset and start executing.
					m68k_pulse_reset();
					cpu_in_reset[i]=0; //it's running now
				}
				//Go execute some m68k code.
				m68k_execute(CPU_SPEED_HZ/(1000000/CPU_RUN_US) + cycles_remaining[i]);
				cycles_remaining[i]=m68k_cycles_remaining();
			}

			if (cur_cpu==0) {
				//handle DMA CPU ints
				for (int i=0; i<4; i++) uart_tick(uart[i], CPU_RUN_US);
				rtc_tick(rtc, CPU_RUN_US);
				scsi_tick(scsi, CPU_RUN_US);
			}

			m68k_get_context(cpuctx[i]);
			if (dump_status) break;
		}
		if (dump_status) {
			//ctrl+\ pressed
			dump_status=0;
			printf("\n");
			printf("Current machine status:\n");
			for (int i=0; i<2; i++) {
				m68k_set_context(cpuctx[i]);
				cur_cpu=i;
				printf("CPU %d\n", i);
				dump_cpu_state_all();
				dump_callstack();
				m68k_get_context(cpuctx[i]);
			}
		}
		if (cfg->realtime) {
			emulated_us_since_last_delay+=CPU_RUN_US;

			//Find out how long we ran in real time
			struct timeval time_since_last_delay;
			struct timeval now;
			gettimeofday(&now, NULL);
			timersub(&now, &last_delay_at, &time_since_last_delay);
			//If this was shorter than the time the virtual CPU spent emulating, we need
			//to wait to make up the difference.
			if (time_since_last_delay.tv_sec!=0 || time_since_last_delay.tv_usec>=SLEEP_EVERY_US) {
				int wait_us=emulated_us_since_last_delay-time_since_last_delay.tv_usec;
				if (wait_us<1000) wait_us=1000; //sleep at least a ms
#ifdef __EMSCRIPTEN__
				emscripten_sleep(wait_us/1000);
#else
				usleep(wait_us);
#endif
				gettimeofday(&last_delay_at, NULL);
				emulated_us_since_last_delay=0;
			}
		}
	}
}


