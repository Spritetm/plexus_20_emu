#include <stdint.h>
#include "mapper.h" //for access flags

void dump_cpu_state();

void emu_raise_int(uint8_t vector, uint8_t level, int cpu);
int emu_get_cur_cpu();

void emu_enable_mapper(int do_enable);

//Returns -1 if not allowed.
int emu_read_byte(int addr);
//Returns false if not allowed.
int emu_write_byte(int addr, int val);

void emu_raise_rtc_int();
int emu_try_mbus_held();

typedef struct {
	const char *u15_rom;
	const char *u17_rom;
} emu_cfg_t;

void emu_start(emu_cfg_t *cfg);

#define EMU_MBUS_ERROR_READ 0x80000000
void emu_mbus_error(unsigned int addr);
