/*
 Simulation of the Multibus memory space.
 Note that this only simulates enough to pass the diags and not crash the 
 kernel. There is no infrastructure for simulating actual Multibus cards.
*/

/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <jeroen@spritesmods.com> wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return. - Sprite_tm
 * ----------------------------------------------------------------------------
 */
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include "emu.h"
#include "log.h"

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

//Returns true if multibus is in held state.
static int mbus_held() {
	if (!emu_try_mbus_held()) return 0;
	MBUS_LOG_DEBUG("Blocking op: bus held\n");
	return 1;
}

void mbus_write8(void *obj, unsigned int a, unsigned int val) {
	MBUS_LOG_DEBUG("MBUS: wb %x->%x %x\n", a, a+0x780000, val);
	if (mbus_held()) {
		MBUS_LOG_NOTICE("MBUS: ^^ write held.\n");
		return;
	}
	if (!emu_get_mb_diag()){
		return;
	}
	int r=emu_write_byte((a+0x780000)^1, val);
	if (r==-1) emu_mbus_error(a);
}

void mbus_write16(void *obj, unsigned int a, unsigned int val) {
	MBUS_LOG_DEBUG("MBUS: ww %x->%x %x\n", a, a+0x780000, val);
	if (mbus_held()) {
		MBUS_LOG_NOTICE("MBUS: ^^ write held.\n");
		return;
	}
	if (!emu_get_mb_diag()){
		return;
	}
	int r=emu_write_byte((a+0x780000), val>>8);
	r+=emu_write_byte((a+0x780001), val);
	if (r<0) emu_mbus_error(a);
}

void mbus_write32(void *obj, unsigned int a, unsigned int val) {
	mbus_write16(obj, a+0, val>>16);
	mbus_write16(obj, a+2, val);
}

unsigned int mbus_read8(void *obj, unsigned int a) {
	MBUS_LOG_DEBUG("MBUS: rb %x->%x\n", a, a+0x780000);
	if (!emu_get_mb_diag()) return 0;
	//Mbus in diag modes errors with a MBTO.
	emu_mbus_error(a|EMU_MBUS_ERROR_READ|EMU_MBUS_BUSERROR);
	return 0;
}

unsigned int mbus_read16(void *obj, unsigned int a) {
	MBUS_LOG_DEBUG("MBUS: rw %x->%x\n", a, a+0x780000);
	if (!emu_get_mb_diag()) return 0;
	//Mbus in diag modes errors with a MBTO.
	emu_mbus_error(a|EMU_MBUS_ERROR_READ|EMU_MBUS_BUSERROR);
	return 0;
}

unsigned int mbus_read32(void *obj, unsigned int a) {
	return (mbus_read16(obj, a)<<16)|mbus_read16(obj, a+2);
}

void mbus_io_write(void *obj, unsigned int a, unsigned int val) {
	if (emu_get_mb_diag()) return;
	MBUS_LOG_DEBUG("mbio wr %x\n", a);
	emu_mbus_error(a|EMU_MBUS_BUSERROR);
}

unsigned int mbus_io_read(void *obj, unsigned int a) {
	if (emu_get_mb_diag()) return 0;
	MBUS_LOG_DEBUG("mbio rd %x\n", a);
	emu_mbus_error(a|EMU_MBUS_ERROR_READ|EMU_MBUS_BUSERROR);
	return 0;
}


