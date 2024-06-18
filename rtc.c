#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include "emu.h"
#include "log.h"
#include "rtc.h"

// Debug logging
#define RTC_LOG(msg_level, format_and_args...) \
	log_printf(LOG_SRC_RTC, msg_level, format_and_args)
#define RTC_LOG_DEBUG(format_and_args...) RTC_LOG(LOG_DEBUG, format_and_args)
#define RTC_LOG_NOTICE(format_and_args...) RTC_LOG(LOG_NOTICE, format_and_args)

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

#define BIT_REGB_SET (1<<7)
#define BIT_REGB_PIE (1<<6)
#define BIT_REGB_AIE (1<<5)
#define BIT_REGB_UIE (1<<4)
#define BIT_REGB_SQWE (1<<3)
#define BIT_REGB_DM (1<<2)
#define BIT_REGB_TWOFOUR (1<<1)
#define BIT_REGB_DSE (1<<0)

#define BIT_REGC_IRQF (1<<7)
#define BIT_REGC_PF (1<<6)
#define BIT_REGC_AF (1<<5)
#define BIT_REGC_UF (1<<4)


//Note we always keep time in binary and convert it when needed.
struct rtc_t {
	uint8_t reg[14];
	int us;
	int intr_us;
	int intr_us_max;
};

static int tobcd(int i) {
	return (i/10)*16+(i%10);
}

static int tobin(int i) {
	return (i>>4)*10+(i&0xf);
}

//Note: This is dead code for the Plexus-20. Turns out the interrupt pin is connected
//to the square wave output...
static void handle_irq(rtc_t *r) {
	int irq;
	irq =((r->reg[CALREGB]&BIT_REGB_PIE) && (r->reg[CALREGC]&BIT_REGC_PF));
	irq|=((r->reg[CALREGB]&BIT_REGB_AIE) && (r->reg[CALREGC]&BIT_REGC_AF));
	irq|=((r->reg[CALREGB]&BIT_REGB_UIE) && (r->reg[CALREGC]&BIT_REGC_UF));
//	if (irq) emu_raise_rtc_int();
}

void rtc_write8(void *obj, unsigned int a, unsigned int val) {
	a=a/2; //rtc is on odd addresses
	rtc_t *r=(rtc_t*)obj;
	static const char *regs[]={"SEC", "SECALRM", "MIN", "MINALRM", "HRS", "HRSALARM", "DAY", "DATE", "MONTH", "YEAR", "A", "B", "C", "D"};
	RTC_LOG_DEBUG("RTC: set %s to 0x%02X\n", regs[a], val&0xff);
	if (a<=CALYEAR) {
		int bcd=r->reg[CALREGB]&1;
		if (bcd) val=tobin(val);
		r->reg[a]=val;
	}
	if (a==CALREGA) {
		val&=0x7F; //no update in progress
		if (((val&0x70)!=0x20) && ((val&0x70)!=0x0)) {
			RTC_LOG_NOTICE("RTC: Warning: unsupported input clock setting\n");
		} else {
			if ((val&0x70)==0x20) {
				static const int tpi_us[16]={
					0, 3906, 7812, 122, 244, 488, 976, 1953, 3906, 7812, 15625, 31250, 62500, 125000, 250000, 500000};
				r->intr_us_max=tpi_us[val&15];
			} else {
				r->intr_us_max=0;
			}
		}
	}
	handle_irq(r);
	if (a!=CALREGC && a!=CALREGD) r->reg[a]=val;
}

void rtc_write16(void *obj, unsigned int a, unsigned int val) {
	rtc_write8(obj, a+1, val);
}

unsigned int rtc_read8(void *obj, unsigned int a) {
	a=a/2; //rtc is on odd addresses
	rtc_t *r=(rtc_t*)obj;
	if (a<=CALYEAR) {
		int bcd=r->reg[CALREGB]&1;
		if (bcd) return tobcd(r->reg[a]); else return r->reg[a];
	}
	int ret=r->reg[a];
	if (a==CALREGC) r->reg[a]=0; //clears on read
	if (a==CALREGD) r->reg[a]=0x80; //set VRT on read
	return ret;
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
		if ((r->reg[CALREGB]&BIT_REGB_SET)==0) {
			r->reg[CALREGC]|=BIT_REGC_UF;
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
			if (r->reg[CALSECS]==r->reg[CALSECALARM] &&
					r->reg[CALMINS]==r->reg[CALMINALARM] &&
					r->reg[CALHRS]==r->reg[CALHRALARM]) {
				r->reg[CALREGC]|=BIT_REGC_AF;
			}
			handle_irq(r);
		}
		r->us-=1000000;
	}
	r->intr_us+=ticklen_us;
	if (r->intr_us_max && r->intr_us>r->intr_us_max) {
		r->intr_us=r->intr_us%r->intr_us_max;
		r->reg[CALREGC]|=BIT_REGC_PF;
		handle_irq(r);

		//handle square wave output, which generates the int on the Plexus-20
		if (r->reg[CALREGB]&BIT_REGB_SQWE) emu_raise_rtc_int();
	}
}

