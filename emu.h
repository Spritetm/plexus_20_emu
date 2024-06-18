#include <stdint.h>

void dump_cpu_state();

void emu_raise_int(uint8_t vector, uint8_t level, int cpu);
int emu_get_cur_cpu();

void emu_enable_mapper(int do_enable);


int emu_read_byte(int addr);
void emu_write_byte(int addr, int val);

void emu_raise_rtc_int();


typedef struct {
	const char *u15_rom;
	const char *u17_rom;
} emu_cfg_t;

void emu_start(emu_cfg_t *cfg);
