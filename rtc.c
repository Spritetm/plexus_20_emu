#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include "emu.h"
#include "rtc.h"

#define	CALSECS		0x00
#define CALSECALARM	0x01
#define CALMINS		0x02
#define CALMINALARM	0x03
#define CALHRS		0x04
#define CALHRALARM	0x05
#define CALDAY		0x06
#define CALDATE		0x07
#define CALMONTH	0x08
#define CALYEAR		0x09

#define CALREGA		0x0A	/* REGA: r/w register */
#define CALREGB		0x0B	/* REGB: r/w register */
#define CALREGC		0x0C	/* REGC: read only register */
#define CALREGD		0x0D	/* REGD: read only register */

//Note we always keep time in binary and convert it when needed.
struct rtc_t {
	uint8_t reg[14];
	int us;
};

static int tobcd(int i) {
	return (i/10)*16+(i%10);
}

static int tobin(int i) {
	return (i>>4)*10+(i&0xf);
}

void rtc_write8(void *obj, unsigned int a, unsigned int val) {
	a=a/2; //rtc is on odd addresses
	rtc_t *r=(rtc_t*)obj;
	if (a<=CALYEAR) {
		int bcd=r->reg[CALREGB]&1;
		if (bcd) val=tobin(val);
		r->reg[a]=val;
	}
	if (a==CALREGA) {
		val&=0x7F; //no update in progress
		printf("RTC: set A to 0x%02X\n", val);
	} else if (a==CALREGB) {
		printf("RTC: set B to 0x%02X\n", val);
	}
	r->reg[a]=val;
}

void rtc_write16(void *obj, unsigned int a, unsigned int val) {
	rtc_write8(obj, a+1, val);
}

unsigned int rtc_read8(void *obj, unsigned int a) {
	rtc_t *r=(rtc_t*)obj;
	if (a<=CALYEAR) {
		int bcd=r->reg[CALREGB]&1;
		if (bcd) return tobcd(r->reg[a]); else return r->reg[a];
	}
	return r->reg[a];
}

unsigned int rtc_read16(void *obj, unsigned int a) {
	return rtc_read8(obj, a+1);
}


rtc_t *rtc_new() {
	rtc_t *ret=calloc(sizeof(rtc_t), 1);
	return ret;
}


void rtc_tick(rtc_t *r, int ticklen_us) {
	r->us+=ticklen_us;
	while (r->us>1000000) {
		if ((r->reg[CALREGB]&0x80)==0) {
			r->reg[CALSECS]++;
			if (r->reg[CALSECS]>=60) {
				r->reg[CALSECS]=0;
				r->reg[CALMINS]++;
			}
			if (r->reg[CALMINS]>=60) {
				r->reg[CALMINS]=0;
				r->reg[CALHRS]++;
			}
			if (r->reg[CALHRS]>=24) {
				r->reg[CALHRS]=0;
				r->reg[CALDAY]++;
				r->reg[CALDATE]++;
			}
			if (r->reg[CALDAY]>=7) {
				r->reg[CALDAY]=0;
			}
			int month=r->reg[CALMONTH];
			if (month>12) month=12;
			const int dim[12]={31,28,31,30,31,30,31,31,30,31,30,31};
			if (r->reg[CALDATE]>=dim[month]) {
				r->reg[CALDATE]=0;
				r->reg[CALMONTH]++;
			}
			if (r->reg[CALMONTH]>=12) {
				r->reg[CALMONTH]=0;
				r->reg[CALYEAR]++;
			}
		}
		r->us-=1000000;
	}
}

