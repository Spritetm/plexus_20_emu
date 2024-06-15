#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include "csr.h"

//csr: usr/include/sys/mtpr.h, note we're Robin

#define CSR_O_RSEL 0x00	/* reset selection , see below */
#define CSR_I_PERR1 0x00	/* parity error latch */
#define CSR_I_PERR2 0x02	/* parity error latch */
#define CSR_I_MBERR 0x04	/* latches address on multibus error */
#define CSR_O_SC_C 0x06	/* scsi byte count */
#define CSR_I_SC_C 0x06	/* scsi byte count */
#define CSR_O_SC_P	0x0A	/* scsi pointer register */
#define CSR_I_SC_P	0x0A	/* scsi pointer register */
#define CSR_O_SC_R	0x0E	/* scsi register */
#define CSR_I_SC_R	0x0E	/* scsi register */
#define CSR_O_LEDS	0x10	/* led register */
#define CSR_I_LEDS	0x10	/* led register */
#define CSR_I_USRT 0x12	/* usart register */
#define CSR_I_ERR	0x14	/* misc functoins */
#define CSR_O_MISC 0x16	/* misc. functions */
#define CSR_I_MISC 0x16	/* misc. functions */
#define CSR_O_KILL 0x18	/* kill job / dma cpu */
#define CSR_I_KILL 0x18	/* kill job / dma cpu */
#define CSR_O_TRCE	0x1A	/* rce/ tce for usarts */
#define CSR_I_TRCE	0x1A	/* rce/ tce for usarts */
#define CSR_O_INTE	0x1C	/* interupt register */
#define CSR_I_INTE	0x1C	/* interupt register */
#define CSR_O_MAPID 0x1E	/* user id register */
#define CSR_I_USER 0x1E	/* user number */

#define	MISC_UINTEN	0x1	/* enable ups interrupt */
#define	MISC_TINTEN	0x2	/* enable temperature interrupt */
#define MISC_CINTJEN	0x4	/* enable job's clock interrupt */
#define MISC_CINTDEN	0x8	/* enable dma's clock interrupt */
#define MISC_RESMB		0x10	/* reset multibus ACTIVE LOW */
#define MISC_HOLDMBUS	0x20	/* hold multibus */
#define MISC_DIAGUART	0x40	/* disable output to ttys */
#define	MISC_TBUSY		0x80	/* READ only */
#define MISC_ENMAP		0x100	/* enable mapping (active low) */
#define MISC_DISMAP	0x100	/* disables map (active hi ) */
#define	MISC_DIAGMB	0x200	/* put multibus into diagnostic mode */
#define MISC_DIAGPESC	0x400	/* force parity scsi parity error */
#define	MISC_DIAGPH	0x800	/* force parity error low byte */
#define MISC_DIAGPL	0x1000	/* force parity error hi byte */
#define MISC_SCSIDL	0x2000	/* enable diag latch (ACTIVE LOW) */
#define MISC_BOOTJOB	0x4000	/* force job's A23 high (ACTIVE LOW ) */
#define MISC_BOOTDMA	0x8000	/* force dma's A23 high (ACTIVE LOW ) */





struct csr_t {
	uint16_t reg[0x10];
};


int csr_cpu_is_reset(csr_t *csr, int cpu) {
	int r=csr->reg[CSR_O_KILL/2] & (1<<cpu);
	if (cpu==1) r=!r; //job kill is low active
	return r;
}

void csr_write16(void *obj, unsigned int a, unsigned int val) {
	csr_t *c=(csr_t*)obj;
	if (a==0) { //reset sel
		printf("csr write16 %x (reset sel) val %x\n", a, val);
	} else if (a==6) { //scsi byte count
	} else if (a==0xA) { //scsi pointer reg
	} else if (a==0xe) { //scsi reg
	} else if (a==0x10) { //led regs
	} else if (a==0x18) { //kill
		printf("csr write16 %x (kill) val %x\n", a, val);
	} else {
//		printf("csr write16 %x val %x\n", a, val);
	}
	c->reg[a/2]=val;
}

void csr_write8(void *obj, unsigned int a, unsigned int val) {
//	printf("csr write8 %x val %x\n", a, val);
	//fake with a csr write16
	if (a&1) {
		csr_write16(obj, a-1, val);
	} else {
		csr_write16(obj, a, val<<8);
	}
}


unsigned int csr_read16(void *obj, unsigned int a) {
	csr_t *c=(csr_t*)obj;
	unsigned int ret=c->reg[a/2];
	if (a==0x18) {
		//note: return 0x80 if we are the job cpu (or if the job cpu is enabled?)
		ret=(c->reg[a/2]);
	}
//	printf("csr read16 0x%X -> 0x%X\n", a, ret);
	return ret;
}

unsigned int csr_read8(void *obj, unsigned int a) {
	//fake using read16
	if (a&1) {
		return csr_read16(obj, a-1);
	} else {
		return csr_read16(obj, a)>>8;
	}
}


csr_t *csr_new() {
	csr_t *ret=calloc(sizeof(csr_t), 1);
	return ret;
}


