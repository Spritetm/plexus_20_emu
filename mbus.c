#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include "emu.h"
#include "log.h"

//Simple and stupid mbus loopback driver. Only good to pass diags.

// Debug logging
#define MBUS_LOG(msg_level, format_and_args...) \
	log_printf(LOG_SRC_MBUS, msg_level, format_and_args)
#define MBUS_LOG_DEBUG(format_and_args...) MBUS_LOG(LOG_DEBUG, format_and_args)
#define MBUS_LOG_INFO(format_and_args...) MBUS_LOG(LOG_INFO, format_and_args)
#define MBUS_LOG_NOTICE(format_and_args...) MBUS_LOG(LOG_NOTICE, format_and_args)

/*
Mbus is an 16-bit Intel bus so LE. M68K is BE. If you write words through 
the loopback, the words mysteriously stay intact even if the endianness 
swaps. So the way they do it is they swap the LE and BE on 16-bit writes/reads, 
but they obvs can't do that for 8-bit writes/reads. So 16-bit writes go through 
transparently, but a write/read to 8-bit address x happens to x^1 :X
*/




static int mbus_held() {
	if (!emu_try_mbus_held()) return 0;
	MBUS_LOG_DEBUG("Blocking op: bus held\n");
	return 1;
}

static int on_wrong_cpu(int addr) {
	int r=emu_get_cur_cpu();
	if (r==1) return 0;
	MBUS_LOG_NOTICE("Mbus access to %x from wrong CPU\n", addr);
	emu_bus_error();
	return 1;
}



void mbus_write8(void *obj, unsigned int a, unsigned int val) {
	if (on_wrong_cpu(a)) return;
	MBUS_LOG_DEBUG("MBUS: wb %x->%x %x\n", a, a+0x780000, val);
	if (mbus_held()) {
		MBUS_LOG_NOTICE("MBUS: ^^ write held.\n");
		return;
	}
	if (!emu_get_mb_diag()){
//		emu_mbus_error(a|EMU_MBUS_ERROR_TIMEOUT);
		return;
	}
	int r=emu_write_byte((a+0x780000)^1, val);
	if (!r) emu_mbus_error(a);
}

void mbus_write16(void *obj, unsigned int a, unsigned int val) {
	if (on_wrong_cpu(a)) return;
	MBUS_LOG_DEBUG("MBUS: ww %x->%x %x\n", a, a+0x780000, val);
	if (mbus_held()) {
		MBUS_LOG_NOTICE("MBUS: ^^ write held.\n");
		return;
	}
	if (!emu_get_mb_diag()){
//		emu_mbus_error(a|EMU_MBUS_ERROR_TIMEOUT);
		return;
	}
	int r=emu_write_byte((a+0x780000), val>>8);
	r&=emu_write_byte((a+0x780001), val);
	if (!r) emu_mbus_error(a);
}

void mbus_write32(void *obj, unsigned int a, unsigned int val) {
	mbus_write16(obj, a+0, val>>16);
	mbus_write16(obj, a+2, val);
}

unsigned int mbus_read8(void *obj, unsigned int a) {
	if (on_wrong_cpu(a)) return 0;
	MBUS_LOG_DEBUG("MBUS: rb %x->%x\n", a, a+0x780000);

	//This sounds silly, but the mbus probably doesn't support reading bytes. The
	//only place in the diags that does that, expects a bus error afterwards.

	emu_mbus_error(a|EMU_MBUS_ERROR_TIMEOUT);
	return 0;
}

unsigned int mbus_read16(void *obj, unsigned int a) {
	if (on_wrong_cpu(a)) return 0;
	MBUS_LOG_DEBUG("MBUS: rw %x->%x\n", a, a+0x780000);
	if (mbus_held()) {
		MBUS_LOG_DEBUG("MBUS: Held read 0x%X\n", a);
		return 0;
	}
	if (!emu_get_mb_diag()){
//		emu_mbus_error(a|EMU_MBUS_ERROR_TIMEOUT);
		return 0;
	}
	int r1=emu_read_byte((a+0x780000));
	int r2=emu_read_byte((a+0x780001));
	if (r1==-1 || r2==-1) {
		emu_mbus_error(a|EMU_MBUS_ERROR_READ);
		r1=0;
		r2=0;
	}
	return (r1<<8)|r2;
}

unsigned int mbus_read32(void *obj, unsigned int a) {
	return (mbus_read16(obj, a)<<16)|mbus_read16(obj, a+2);
}

void mbus_io_write(void *obj, unsigned int a, unsigned int val) {
	if (emu_get_mb_diag()) return;
//	printf("mbio wr %x\n", a);
	emu_mbus_error(a|EMU_MBUS_ERROR_TIMEOUT);
}

unsigned int mbus_io_read(void *obj, unsigned int a) {
	if (emu_get_mb_diag()) return 0;
	emu_mbus_error(a|EMU_MBUS_ERROR_TIMEOUT);
	return 0;
}


