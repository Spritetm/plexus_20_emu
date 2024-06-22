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
void emu_bus_error();
void emu_set_cur_mapid(uint8_t mapid);
void emu_set_force_a23(int val);
void emu_set_force_parity_error(int val);
void emu_set_mb_diag(int ena);
int emu_get_mb_diag();

typedef struct {
	const char *u15_rom;
	const char *u17_rom;
	const char *hd0img;
} emu_cfg_t;

void emu_start(emu_cfg_t *cfg);

#define EMU_MBUS_ERROR_READ 0x80000000
#define EMU_MBUS_ERROR_TIMEOUT 0x40000000
void emu_mbus_error(unsigned int addr);
