/*
 Simulation of the non-volatile RAM in the MC146818 RTC chip.
*/

/*

ToDo: license

 */
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include "emu.h"
#include "log.h"
#include "rtcram.h"
#include "emscripten_env.h"

// Debug logging (shared with the RTC clock portion)
#define RTC_LOG(msg_level, format_and_args...) \
        log_printf(LOG_SRC_RTC, msg_level, format_and_args)
#define RTC_LOG_DEBUG(format_and_args...)   RTC_LOG(LOG_DEBUG,   format_and_args)
#define RTC_LOG_INFO(format_and_args...)    RTC_LOG(LOG_INFO,    format_and_args)
#define RTC_LOG_NOTICE(format_and_args...)  RTC_LOG(LOG_NOTICE,  format_and_args)
#define RTC_LOG_WARNING(format_and_args...) RTC_LOG(LOG_WARNING, format_and_args)

struct rtcram_t {
	uint8_t reg[64];
	const char *filename;
};

void rtcram_write8(void *obj, unsigned int a, unsigned int val) {
	rtcram_t *r=(rtcram_t*)obj;
	FILE *rtcramfile = NULL;
	size_t written = 0;

	a=a/2; //rtc is on odd addresses
	assert(a < sizeof(r->reg));

	r->reg[a]=val;
	RTC_LOG_DEBUG("RTC: wrote 0x%02x to RAM position 0x%02x\n", val, a);

	if ((rtcramfile = fopen(r->filename, "wb"))) {
		written = fwrite(r->reg, sizeof(r->reg), 1, rtcramfile);
		fclose(rtcramfile);
	}

	if (written == 1) {
		RTC_LOG_DEBUG("RTC: RAM persisted to %s\n", r->filename);
	} else {
		RTC_LOG_WARNING("RTC: Failed to persist RTC RAM to %s\n", r->filename);
	}
#ifdef __EMSCRIPTEN__
	emscripten_syncfs();
#endif
}

void rtcram_write16(void *obj, unsigned int a, unsigned int val) {
	rtcram_write8(obj, a+1, val);
}

// RTC RAM is at every second address, so write32 writes to
// - lower 8 bits of top 16 bits
// - lower 8 bits of bottom 16 bits
void rtcram_write32(void *obj, unsigned int a, unsigned int val) {
	rtcram_write8(obj, a+1, (val >> 16) & 0xFFFF); // lower 8 of upper 16
	rtcram_write8(obj, a+3,  val        & 0xFFFF); // lower 8 of lower 16
}

unsigned int rtcram_read8(void *obj, unsigned int a) {
	rtcram_t *r=(rtcram_t*)obj;

	a=a/2; //rtc is on odd addresses
	assert(a < sizeof(r->reg));

	int ret=r->reg[a];
	return ret;
}

unsigned int rtcram_read16(void *obj, unsigned int a) {
	return rtcram_read8(obj, a+1);
}

// RTC RAM is at every second address, so read32 reads from
// - lower 8 bits of top 16 bits
// - lower 8 bits of bottom 16 bits
unsigned int rtcram_read32(void *obj, unsigned int a) {
	return (((rtcram_read8(obj, a+1) & 0XFFFF) << 16) |
	        ((rtcram_read8(obj, a+3) & 0xFFFF)));
}

rtcram_t *rtcram_new(const char *filename) {
	rtcram_t *r=calloc(sizeof(rtcram_t), 1);
	r->filename=strdup(filename);

	FILE *rtcramfile = NULL;
	size_t rtcread   = 0;

	if ((rtcramfile = fopen(r->filename, "rb"))) {
		rtcread = fread(r->reg, sizeof(r->reg), 1, rtcramfile);
		fclose(rtcramfile);
	}

	if (rtcread == 1) {
		RTC_LOG_INFO("RTC: Loaded persistent RTC RAM from %s\n", r->filename);
	} else {
		RTC_LOG_NOTICE("RTC: Unable to load persistent RTC RAM from %s; using zeros\n",
				r->filename);
	}

	return r;
}
