#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include "emu.h"
#include "log.h"
#include "rtcram.h"

// Debug logging (shared with the RTC clock portion)
#define RTC_LOG(msg_level, format_and_args...) \
        log_printf(LOG_SRC_RTC, msg_level, format_and_args)
#define RTC_LOG_DEBUG(format_and_args...)  RTC_LOG(LOG_DEBUG,  format_and_args)
#define RTC_LOG_INFO(format_and_args...)   RTC_LOG(LOG_INFO,   format_and_args)
#define RTC_LOG_NOTICE(format_and_args...) RTC_LOG(LOG_NOTICE, format_and_args)

struct rtcram_t {
	uint8_t reg[64];
};

void rtcram_write8(void *obj, unsigned int a, unsigned int val) {
	rtcram_t *r=(rtcram_t*)obj;

	a=a/2; //rtc is on odd addresses
	assert(a < sizeof(r->reg));

	r->reg[a]=val;
	RTC_LOG_DEBUG("RTC: wrote 0x%02x to RAM position 0x%02x\n", val, a);
	RTC_LOG_INFO("RTC RAM write is not yet persisted to disk\n");
}

void rtcram_write16(void *obj, unsigned int a, unsigned int val) {
	rtcram_write8(obj, a+1, val);
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

rtcram_t *rtcram_new() {
	rtcram_t *ret=calloc(sizeof(rtcram_t), 1);
	RTC_LOG_INFO("RTC RAM is not yet loaded from disk\n");
	// TODO: load RTC RAM from disk if available
	return ret;
}
