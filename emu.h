
void dump_cpu_state();

void emu_raise_int(int vector, int level, int cpu);
int emu_get_cur_cpu();

void emu_enable_mapper(int do_enable);


int emu_read_byte(int addr);
int emu_write_byte(int addr, int val);

