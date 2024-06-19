#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include "csr.h"
#include "emu.h"
#include "log.h"
#include "scsi.h"

// Debug logging
#define CSR_LOG(msg_level, format_and_args...) \
        log_printf(LOG_SRC_CSR, msg_level, format_and_args)
#define CSR_LOG_DEBUG(format_and_args...) CSR_LOG(LOG_DEBUG, format_and_args)
#define CSR_LOG_INFO(format_and_args...) CSR_LOG(LOG_INFO, format_and_args)
#define CSR_LOG_WARN(format_and_args...) CSR_LOG(LOG_WARNING, format_and_args)

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
#define CSR_I_ERR	0x14	/* error reporting */
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
#define KILL_JKPD 0x40 //job control protection disable
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


#define ERR_AS26			0x8000 //deadman timer for all DMA transfers
#define ERR_SOOPS			0x4000 //Any DMA access of multibus or map
#define ERR_UBE_DMA			0x1000 //User ID mismatch
#define ERR_ABE_DMA			0x0800 //Privilege violation
#define ERR_EN_BLK			0x0400
#define ERR_EN_DMA			0x0200
#define ERR_EN_JOB			0x0100
#define ERR_AERR_JOB		0x0080 //User in system space
#define ERR_DERR_JOB		0x0040 //Generated when job CPU accesses DMA bus
#define ERR_MBTO			0x0020 //Multibus timeout
#define ERR_UBE_JOB			0x0010 //User ID mismatch
#define ERR_ABE_JOB			0x0008 //Privilege violation
#define ERR_EN_JOB2			0x0004 //duplicate with 0x100?
#define ERR_EN_BLK2			0x0002 //duplicate with 0x200?
#define ERR_EN_MBUS			0x0001


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
	if (cpu==0) {
		return (csr->reg[CSR_O_MISC/2]&MISC_CINTDEN);
	} else {
		return (csr->reg[CSR_O_MISC/2]&MISC_CINTJEN);
	}
}

int csr_try_mbus_held(csr_t *csr) {
	if (csr->reg[CSR_O_MISC/2]&MISC_HOLDMBUS) {
		csr->reg[CSR_O_MISC/2]|=MISC_TBUSY;
		return 0;
	}
	return 1;
}

void cst_set_access_error(csr_t *csr, int cpu, int type) {
	int v=0;
	if (cpu==0) {
		if (type&ACCESS_ERROR_U) v|=ERR_UBE_DMA;
		if (type&ACCESS_ERROR_A) v|=ERR_ABE_DMA;
	} else {
		if (type&ACCESS_ERROR_U) v|=ERR_UBE_JOB;
		if (type&ACCESS_ERROR_A) v|=ERR_ABE_JOB;
	}
	csr->reg[CSR_I_ERR/2]|=v;
}

void csr_write16(void *obj, unsigned int a, unsigned int val) {
	csr_t *c=(csr_t*)obj;
	if (a==CSR_O_RSEL) {
		CSR_LOG_DEBUG("csr write16 0x%X (reset sel) val 0x%X\n", a, val);
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
		if ((val&MISC_HOLDMBUS)==0) {
			val&=~MISC_TBUSY;
		}
		int v=0;
		if ((val&MISC_SCSIDL)==0) v|=SCSI_DIAG_LATCH;
		if ((val&MISC_DIAGPESC)) v|=SCSI_DIAG_PARITY;
		scsi_set_diag(c->scsi, v);
		v=0;
		if (!(val&MISC_BOOTDMA)) v|=1;
		if (!(val&MISC_BOOTJOB)) v|=2;
		emu_set_force_a23(v);
	} else if (a==CSR_O_KILL) { //kill
		CSR_LOG_DEBUG("csr write16 0x%X (kill) val 0x%X\n", a, val);
		assert((val&0x40)==0); //we don't support this bit yet but sw doesn't seem to use it
		val&=0x43; //rest is set elsewhere
	} else if (a==CSR_I_ERR) {
		CSR_LOG_DEBUG("csr write16 0x%X (err) val 0x%X - reg is RO?\n", a, val);
		val=c->reg[a/2];
	} else if (a==CSR_O_MAPID) {
		emu_set_cur_mapid(val>>8);
	} else {
		CSR_LOG_DEBUG("csr write16 0x%X val 0x%X\n", a, val);
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
	if (a<4) CSR_LOG_WARN("Read from unknown reg %x\n", a);
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
	} else {
		CSR_LOG_DEBUG("csr read16 0x%X -> 0x%X\n", a, ret);
	}
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
	} else if (a==RESET_MULTERR) {
		CSR_LOG_DEBUG("CSR: Reset mbus error\n");
		c->reg[CSR_O_MISC]&=~MISC_TBUSY;
	} else if (a==RESET_JBERR) {
		CSR_LOG_DEBUG("CSR: Reset job bus error\n");
		c->reg[CSR_I_ERR/2]&=~(ERR_UBE_JOB|ERR_ABE_JOB);
	} else if (a==RESET_DBERR) {
		CSR_LOG_DEBUG("CSR: Reset dma bus error\n");
		c->reg[CSR_I_ERR/2]&=~(ERR_UBE_DMA|ERR_ABE_DMA);
	} else if (a==RESET_CINTJ || a==RESET_CINTJ) {
		//do nothing, our implementation doesn't need this reset.
	} else {
		CSR_LOG_DEBUG("Unhandled MMIO write 0x%x\n", a);
	}
}

void csr_raise_error(csr_t *c, int error, unsigned int addr) {
	if (error==CSR_ERR_MBUS) {
		emu_raise_int(0x7F, 1, 1);
		c->reg[CSR_I_MBERR/2]=(addr>>11)&0xfe;
		if (addr&EMU_MBUS_ERROR_READ) c->reg[CSR_I_MBERR/2]|=0x1;
	}
}


csr_t *csr_new(scsi_t *scsi) {
	csr_t *ret=calloc(sizeof(csr_t), 1);
	ret->scsi=scsi;
	return ret;
}


