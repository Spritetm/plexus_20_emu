#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include "log.h"
#include "ramrom.h"

struct ram_t {
	int size_bytes;
	int amask;
	uint8_t *buffer;
};

// Debug logging
#define RAMROM_LOG(msg_level, format_and_args...) \
	log_printf(LOG_SRC_RAMROM, msg_level, format_and_args)
#define RAMROM_LOG_DEBUG(format_and_args...)   RAMROM_LOG(LOG_DEBUG,   format_and_args)
#define RAMROM_LOG_WARNING(format_and_args...) RAMROM_LOG(LOG_WARNING, format_and_args)

void ram_write8(void *obj, unsigned int a, unsigned int val) {
	ram_t *ram=(ram_t*)obj;
	a=a&ram->amask;
	ram->buffer[a]=val;
}

void ram_write16(void *obj, unsigned int a, unsigned int val) {
	ram_t *ram=(ram_t*)obj;
	a=a&ram->amask;
	ram->buffer[a]=(val>>8);
	ram->buffer[a+1]=val;
}

void ram_write32(void *obj, unsigned int a, unsigned int val) {
	ram_t *ram=(ram_t*)obj;
	a=a&ram->amask;
	ram->buffer[a]=(val>>24);
	ram->buffer[a+1]=(val>>16);
	ram->buffer[a+2]=(val>>8);
	ram->buffer[a+3]=val;
}

unsigned int ram_read8(void *obj, unsigned int a) {
	ram_t *ram=(ram_t*)obj;
	a=a&ram->amask;
	return ram->buffer[a];
}

unsigned int ram_read16(void *obj, unsigned int a) {
	ram_t *ram=(ram_t*)obj;
	a=a&ram->amask;
	return ram->buffer[a+1]+(ram->buffer[a]<<8);
}

unsigned int ram_read32(void *obj, unsigned int a) {
	ram_t *ram=(ram_t*)obj;
	a=a&ram->amask;
	return ram->buffer[a+3]+(ram->buffer[a+2]<<8)+(ram->buffer[a+1]<<16)+(ram->buffer[a]<<24);
}

ram_t *rom_new(const char *filename, int size_bytes) {
	FILE *f=fopen(filename, "rb");
	if (!f) {
		perror(filename);
		exit(1);
	}
	ram_t *rom=ram_new(size_bytes);
	int r=fread(rom->buffer, 1, size_bytes, f);
	fclose(f);
	if (r!=size_bytes) {
		RAMROM_LOG_WARNING("%s: short read: %d bytes for rom region of %d bytes\n", filename, r, size_bytes);
	}
	return rom;
}

ram_t *ram_new(int size_bytes) {
	if (size_bytes & (size_bytes-1)) {
		printf("ram_new: size should be power of two\n");
		exit(0);
	}
	ram_t *ram=calloc(sizeof(ram_t), 1);
	ram->size_bytes=size_bytes;
	ram->amask=(size_bytes-1); //works if size_bytes is power of two
	ram->buffer=calloc(size_bytes, 1);
	return ram;
}

