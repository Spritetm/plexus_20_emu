#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include "emu.h"

//Simple and stupid mbus loopback driver. Only good to pass diags.

/*
Mbus is an 16-bit Intel bus so LE. M68K is BE. If you write words through 
the loopback, the words mysteriously stay intact even if the endianness 
swaps. So the way they do it is they swap the LE and BE on 16-bit writes/reads, 
but they obvs can't do that for 8-bit writes/reads. So 16-bit writes go through 
transparently, but a write/read to 8-bit address x happens to x^1 :X
*/

void mbus_write8(void *obj, unsigned int a, unsigned int val) {
//	printf("MBUS: w %x->%x %x\n", a, a+0x780000, val);
	emu_write_byte((a+0x780000)^1, val);
}

void mbus_write16(void *obj, unsigned int a, unsigned int val) {
	emu_write_byte((a+0x780000), val>>8);
	emu_write_byte((a+0x780001), val);
}

void mbus_write32(void *obj, unsigned int a, unsigned int val) {
	mbus_write16(obj, a+0, val>>16);
	mbus_write16(obj, a+2, val);
}

unsigned int mbus_read8(void *obj, unsigned int a) {
	return emu_read_byte((a+0x780000)^1);
}

unsigned int mbus_read16(void *obj, unsigned int a) {
	unsigned int r;
	r=emu_read_byte((a+0x780000))<<8;
	r|=emu_read_byte((a+0x780001));
	return r;
}

unsigned int mbus_read32(void *obj, unsigned int a) {
	return (mbus_read16(obj, a)<<16)|mbus_read16(obj, a+2);
}

