#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include "log.h"

// Debug logging
#define RAMROM_LOG(msg_level, format_and_args...) \
	log_printf(LOG_SRC_RAMROM, msg_level, format_and_args)
#define RAMROM_LOG_DEBUG(format_and_args...)   RAMROM_LOG(LOG_DEBUG,   format_and_args)
#define RAMROM_LOG_WARNING(format_and_args...) RAMROM_LOG(LOG_WARNING, format_and_args)

void ram_write8(void *obj, unsigned int a, unsigned int val) {
	uint8_t *buffer=(uint8_t*)obj;
	buffer[a]=val;
}

void ram_write16(void *obj, unsigned int a, unsigned int val) {
	uint8_t *buffer=(uint8_t*)obj;
	buffer[a]=(val>>8);
	buffer[a+1]=val;
}

void ram_write32(void *obj, unsigned int a, unsigned int val) {
	uint8_t *buffer=(uint8_t*)obj;
	buffer[a]=(val>>24);
	buffer[a+1]=(val>>16);
	buffer[a+2]=(val>>8);
	buffer[a+3]=val;
}

unsigned int ram_read8(void *obj, unsigned int a) {
	uint8_t *buffer=(uint8_t*)obj;
	return buffer[a];
}

unsigned int ram_read16(void *obj, unsigned int a) {
	uint8_t *buffer=(uint8_t*)obj;
	return buffer[a+1]+(buffer[a]<<8);
}

unsigned int ram_read32(void *obj, unsigned int a) {
	uint8_t *buffer=(uint8_t*)obj;
	return buffer[a+3]+(buffer[a+2]<<8)+(buffer[a+1]<<16)+(buffer[a]<<24);
}

void *rom_new(const char *filename, int size) {
	FILE *f=fopen(filename, "rb");
	if (!f) {
		perror(filename);
		exit(1);
	}
	char *buf=malloc(size);
	int r=fread(buf, 1, size, f);
	fclose(f);
	if (r!=size) {
		RAMROM_LOG_WARNING("%s: short read: %d bytes for rom region of %d bytes\n", filename, r, size);
	}
	return buf;
}

void *ram_new(int size) {
	char *buf=calloc(size, 1);
	return buf;
}

