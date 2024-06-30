#include <stdint.h>
#include "mapper.h" //for access flags

//Debug functions: dump the state, dump a very verbose state, dump the callstack
void dump_cpu_state();
void dump_cpu_state_all();
void dump_callstack();

//Raise an interrupt for a vector. Use level=0 to clear the interrupt.
void emu_raise_int(uint8_t vector, uint8_t level, int cpu);

//returns 0 for DMA, 1 for JOB cpu
int emu_get_cur_cpu();

//Called by CSR to enable mapper.
void emu_enable_mapper(int do_enable);


//Memory read/write functions for MBUS and SCSI.
//Returns -1 if not allowed.
int emu_read_byte(int addr);
//Returns false (0) if not allowed.
int emu_write_byte(int addr, int val);

//Called when the RTC square wave input goes low->high
void emu_raise_rtc_int();
//Called to check if the MBUS is held. Changes TBUSY accordingly.
int emu_try_mbus_held();
//Triggers a bus error on the current CPU. Note: may not return
//as the bus error code involves a longjmp().
void emu_bus_error();
//Set the current map ID for the mapper.
void emu_set_cur_mapid(uint8_t mapid);
//Set the 'force a23 high' bit for the CPUs. val=bitmask: bit0=dma, bit1=job
void emu_set_force_a23(int val);
//Force a parity bit. val is a bitmask: bit0 forces parity error for low byte, bit1 for high byte.
void emu_set_force_parity_error(int val);
//Set Multibus diag loopback status
void emu_set_mb_diag(int ena);
//Return Multibus diag loopback status.
int emu_get_mb_diag();

//Emulation configuration.
typedef struct {
	const char *u15_rom;	//Filename for U15 ROM contents
	const char *u17_rom;	//Filename for U17 ROM contents
	const char *hd0img;		//Filename for hard disk image
	const char *rtcram;		//Filename for RTC NVRAM storage file
	int realtime;			//If true, we sleep() to make performance equal to that of a real machine
	const char *cow_dir;	//Directory path for COW files, or "" or NULL for no COW
	int mem_size_bytes;		//Main RAM memory size
	int noyolo;				//True to disable YOLO hack
} emu_cfg_t;

//Start emu with given parameters
void emu_start(emu_cfg_t *cfg);

//Tells the emulator an interrupt is going to happen in x uS. Emulator will adjust
//CPU execution schedule to make sure an interrupt check happens at that time.
void emu_schedule_int_us(int us);

//Handle a multibus error. addr is the OR of the fault address and the following flags:
#define EMU_MBUS_ERROR_READ 0x80000000
#define EMU_MBUS_BUSERROR 0x40000000
void emu_mbus_error(unsigned int addr);
