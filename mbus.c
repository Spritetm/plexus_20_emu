#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include "emu.h"

//Simple and stupid mbus loopback driver. Only good to pass diags.

void mbus_write8(void *obj, unsigned int a, unsigned int val) {
	emu_write_byte(a+0x780000, val);
}

void mbus_write16(void *obj, unsigned int a, unsigned int val) {
	mbus_write8(obj, a, val>>8);
	mbus_write8(obj, a+1, val);
}

void mbus_write32(void *obj, unsigned int a, unsigned int val) {
	mbus_write8(obj, a, val>>24);
	mbus_write8(obj, a+1, val>>16);
	mbus_write8(obj, a+2, val>>8);
	mbus_write8(obj, a+3, val);
}

unsigned int mbus_read8(void *obj, unsigned int a) {
	return emu_read_byte(a+0x780000);
}

unsigned int mbus_read16(void *obj, unsigned int a) {
	return (mbus_read8(obj, a)<<8)|mbus_read8(obj, a+1);
}

unsigned int mbus_read32(void *obj, unsigned int a) {
	return (mbus_read16(obj, a)<<16)|mbus_read16(obj, a+2);
}
