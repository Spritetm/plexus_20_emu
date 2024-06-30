#ifndef RTCRAM_H
#define RTCRAM_H

typedef struct rtcram_t rtcram_t;

//Memory range access handlers
void rtcram_write8(void *obj, unsigned int a, unsigned int val);
void rtcram_write16(void *obj, unsigned int a, unsigned int val);
void rtcram_write32(void *obj, unsigned int a, unsigned int val);
unsigned int rtcram_read8(void *obj, unsigned int a);
unsigned int rtcram_read16(void *obj, unsigned int a);
unsigned int rtcram_read32(void *obj, unsigned int a);

rtcram_t *rtcram_new(const char *filename);

#endif
