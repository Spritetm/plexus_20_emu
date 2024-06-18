#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include "csr.h"
#include "emu.h"
#include "log.h"
#include "scsi.h"

// Debug logging
#define CSR_LOG(msg_level, format_and_args...) \
        log_printf(LOG_SRC_CSR, msg_level, format_and_args)
#define CSR_LOG_DEBUG(format_and_args...) CSR_LOG(LOG_DEBUG, format_and_args)

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


#define KILL_KILL_DMA 0x1
#define KILL_NKILL_JOB 0x2
#define KILL_INT_DMA 0x4
#define KILL_INT_JOB 0x8
#define KILL_CUR_IS_JOB 0x80


#define	RESET_MULTERR		0x020  /* reset multibus interface error flag */
#define	RESET_SCSI_PFLG		0x040  /* reset scsi parity error flag */
#define	RESET_CLR_JOBINT	0x060  /* reset job processor software int */
#define	RESET_SET_JOBINT	0x080  /* set job processor software int */
#define	RESET_CLR_DMAINT	0x0a0  /* reset dma processor software int */
#define	RESET_SET_DMAINT	0x0c0  /* set dma processor int */
#define	RESET_CINTJ			0x0e0  /* reset job clock interrupt */
#define RESET_CINTD			0x100  /* reset dma clock int */
#define	RESET_JBERR			0x120  /* reset job bus error flag */
#define	RESET_DBERR			0x140  /* reset dma bus error flag */
#define	RESET_MPERR			0x160  /* reset memory parity err flag SET ON RESET*/
#define	RESET_SWINT			0x180  /* reset switch interrupt */
#define	RESET_SCSIBERR		0x1a0  /* reset scsi bus error flag */


struct csr_t {
	uint16_t reg[0x10];
	scsi_t *scsi;
};


int csr_cpu_is_reset(csr_t *csr, int cpu) {
	int r=csr->reg[CSR_O_KILL/2] & (1<<cpu);
	if (cpu==1) r=!r; //job kill is low active
	return r;
}

int csr_get_rtc_int_ena(csr_t *csr, int cpu) {
	//Note - unlike everything the docs say, these seem to be active low.
	if (cpu==0) {
		return !(csr->reg[CSR_O_MISC]&MISC_CINTDEN);
	} else {
		return !(csr->reg[CSR_O_MISC]&MISC_CINTJEN);
	}
}

void csr_write16(void *obj, unsigned int a, unsigned int val) {
	csr_t *c=(csr_t*)obj;
	if (a==CSR_O_RSEL) {
		CSR_LOG_DEBUG("csr write16 %x (reset sel) val %x\n", a, val);
	} else if (a==CSR_O_SC_C || a==CSR_O_SC_C+2) {
		c->reg[a/2]=val;
		scsi_set_bytecount(c->scsi, ((c->reg[CSR_O_SC_C/2]<<16)+c->reg[CSR_O_SC_C/2+1])&0xffffff);
	} else if (a==CSR_O_SC_P || a==CSR_O_SC_P+2) {
		c->reg[a/2]=val;
		scsi_set_pointer(c->scsi, ((c->reg[CSR_O_SC_P/2]<<16)+c->reg[CSR_O_SC_P/2+1])&0xffffff);
	} else if (a==CSR_O_SC_R) {
		scsi_set_scsireg(c->scsi, val);
	} else if (a==CSR_O_MISC) {
		emu_enable_mapper(!(val&MISC_ENMAP));
		int v=0;
		if ((val&MISC_SCSIDL)==0) v|=SCSI_DIAG_LATCH;
		if ((val&MISC_DIAGPESC)) v|=SCSI_DIAG_PARITY;
		scsi_set_diag(c->scsi, v);
	} else if (a==CSR_O_KILL) { //kill
		CSR_LOG_DEBUG("csr write16 %x (kill) val %x\n", a, val);
		val&=0x3; //rest is set elsewhere
	} else {
		CSR_LOG_DEBUG("csr write16 %x val %x\n", a, val);
	}
	c->reg[a/2]=val;
}

void csr_write32(void *obj, unsigned int a, unsigned int val) {
	csr_write16(obj, a, val>>16);
	csr_write16(obj, a+2, val&0xffff);
}


void csr_write8(void *obj, unsigned int a, unsigned int val) {
	CSR_LOG_DEBUG("csr write8 %x val %x\n", a, val);
	//fake with a csr write16
	if (a&1) {
		csr_write16(obj, a-1, val);
	} else {
		csr_write16(obj, a, val<<8);
	}
}


unsigned int csr_read16(void *obj, unsigned int a) {
	csr_t *c=(csr_t*)obj;
	int b=scsi_get_bytecount(c->scsi);
	c->reg[CSR_O_SC_C/2]=b>>16;
	c->reg[CSR_O_SC_C/2+1]=b;
	b=scsi_get_pointer(c->scsi);
	c->reg[CSR_O_SC_P/2]=b>>16;
	c->reg[CSR_O_SC_P/2+1]=b;

	unsigned int ret=c->reg[a/2];
	if (a==CSR_O_KILL) {
		//note: return 0x80 if we are the job cpu
		if (emu_get_cur_cpu()) ret|=0x80;
	} else if (a==CSR_O_SC_R) {
		return scsi_get_scsireg(c->scsi);
	}
	CSR_LOG_DEBUG("csr read16 0x%X -> 0x%X\n", a, ret);
	return ret;
}

unsigned int csr_read32(void *obj, unsigned int a) {
	return (csr_read16(obj, a)<<16)+csr_read16(obj, a+2);
}

unsigned int csr_read8(void *obj, unsigned int a) {
	//fake using read16
	if (a&1) {
		return csr_read16(obj, a-1);
	} else {
		return csr_read16(obj, a)>>8;
	}
}

#define INTVECT_DMA 0xc2
#define INTVECT_JOB 0xc1

void csr_write16_mmio(void *obj, unsigned int a, unsigned int val) {
	csr_t *c=(csr_t*)obj;
	//note: a has the start of MMIO as base, but RESET_* has the base of CSR,
	//so we adjust the address here.
	a=a+0x20;
	if (a==RESET_CLR_JOBINT) {
		CSR_LOG_DEBUG("CSR: Clear job int\n");
		c->reg[CSR_O_KILL/2] &= ~KILL_INT_JOB;
		emu_raise_int(INTVECT_JOB, 0, 1);
	} else if (a==RESET_SET_JOBINT) {
		CSR_LOG_DEBUG("CSR: Set job int\n");
		c->reg[CSR_O_KILL/2] |= KILL_INT_JOB;
		emu_raise_int(INTVECT_JOB, 4, 1);
	} else if (a==RESET_CLR_DMAINT) {
		CSR_LOG_DEBUG("CSR: Clear dma int\n");
		c->reg[CSR_O_KILL/2] &= ~KILL_INT_DMA;
		emu_raise_int(INTVECT_DMA, 0, 0);
	} else if (a==RESET_SET_DMAINT) {
		CSR_LOG_DEBUG("CSR: Set dma int\n");
		c->reg[CSR_O_KILL/2] |= KILL_INT_DMA;
		emu_raise_int(INTVECT_DMA, 2, 0);
	} else {
		CSR_LOG_DEBUG("Unhandled MMIO write 0x%x\n", a);
	}
}


csr_t *csr_new(scsi_t *scsi) {
	csr_t *ret=calloc(sizeof(csr_t), 1);
	ret->scsi=scsi;
	return ret;
}


