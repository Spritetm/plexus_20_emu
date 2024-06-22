#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include "scsi.h"
#include "emu.h"
#include "log.h"
#include "int.h"

// Debug logging
#define SCSI_LOG(msg_level, format_and_args...) \
	log_printf(LOG_SRC_SCSI, msg_level, format_and_args)
#define SCSI_LOG_DEBUG(format_and_args...)  SCSI_LOG(LOG_DEBUG,  format_and_args)
#define SCSI_LOG_INFO(format_and_args...)   SCSI_LOG(LOG_INFO,   format_and_args)
#define SCSI_LOG_NOTICE(format_and_args...) SCSI_LOG(LOG_NOTICE, format_and_args)

struct scsi_t {
	scsi_dev_t *dev[8];
	uint8_t buf[4];
	int bytecount;
	int pointer;
	int reg;
	int state;
	int byte_stashed;
	int diag;
	int last_req_reg;
	int ptr_read_msb;
	int sel_tgt;
	uint8_t cmd[10];
	int selected;
	int op_timeout_us;
	uint8_t databuf[4096]; //todo: dynamic resize?
};

/*
scsi buf:
0 - R/W: input buf hi byte
1 - R/W: input buf low byte
2 - R: input buf hi byte  W: SCSI data buf h
3 - R: input buf low byte W: SCSI databuf l
*/

static void handle_interrupts(scsi_t *s);

void scsi_write16(void *obj, unsigned int a, unsigned int val) {
	SCSI_LOG_DEBUG("SCSI buf: ww 0x%X 0x%X\n", a, val);
	scsi_t *c=(scsi_t*)obj;
	c->buf[a]=val>>8;
	c->buf[a+1]=val;
}

void scsi_write8(void *obj, unsigned int a, unsigned int val) {
	SCSI_LOG_DEBUG("SCSI buf: wb 0x%X 0x%X\n", a, val);
	scsi_t *c=(scsi_t*)obj;
	c->buf[a]=val;
}

unsigned int scsi_read16(void *obj, unsigned int a) {
	scsi_t *c=(scsi_t*)obj;
	int ret=(c->buf[a]<<8)+c->buf[a+1];
	SCSI_LOG_DEBUG("SCSI buf: rw 0x%X 0x%X\n", a, ret);
	return ret;
}

unsigned int scsi_read8(void *obj, unsigned int a) {
	scsi_t *c=(scsi_t*)obj;
	int ret=c->buf[a];
	SCSI_LOG_DEBUG("SCSI buf: rb 0x%X 0x%X\n", a, ret);
	return ret;
}

void scsi_set_bytecount(scsi_t *s, int bytecount) {
	SCSI_LOG_DEBUG("SCSI: Bytecount 0x%X\n", bytecount);
	s->bytecount=bytecount;
}

void scsi_set_pointer(scsi_t *s, int pointer) {
	SCSI_LOG_DEBUG("SCSI: Pointer 0x%X\n", pointer);
	s->pointer=pointer;
	s->ptr_read_msb=(pointer&1);
}

int scsi_get_bytecount(scsi_t *s) {
	return s->bytecount;
}

int scsi_get_pointer(scsi_t *s) {
	//Not sure. This may also be the amount of bytes written/read.
	return s->pointer;
}

#define O_IOPTR		0x8000
#define O_MSGPTR	0x4000
#define O_CDPTR		0x2000
#define O_SRAM		0x1000
#define O_RESET		0x0800
#define O_SELENA	0x0400
#define O_SCSIBSY	0x0200
#define O_ARB		0x0100
#define O_SCSIREQ	0x0080
#define O_SCSIMSG	0x0040
#define O_SCSIRST	0x0020
#define O_SCSIIO	0x0010 //true indicates input to the initiator
#define O_SCSICD	0x0008 //true indicates control
#define O_SCSIATN	0x0004
#define O_SCSIACK	0x0002
#define O_AUTOXFR	0x0001

#define I_NARBR		0x8000
#define I_SCZERO	0x4000
#define I_NSCPERR	0x2000
#define I_NSCBERR	0x1000
#define I_STIME		0x0800
#define I_SEL		0x0400
#define I_BSY		0x0200
#define I_MYBIT		0x0100
#define I_REQ		0x0080
#define I_MSG		0x0040
#define I_SCRST		0x0020
#define I_IO		0x0010
#define I_CD		0x0008
#define I_ATN		0x0004
#define I_ACK		0x0002
#define I_DATEN		0x0001

/*
 SCSI state machine
 It looks like the CPU twiddles bits immediately, but there is some 'hardware acceleration': the 
 arb process is automated (you set ARB, it raises a SELECT int), and there is a bit of DMA:
 when AUTOXFR is set, bits get read from *pointer into SCSIBUF.


 DMA seems to write 16 bit at a time: writes to an even byte are stashed, as soon as the odd 
 byte is written, both writes go through. (I think.)

 NOTE:
 - ATN is driven by the initiator, and tells the target that we want to send it a message.
 - CMD, IO and MSG are driven by the target.
*/

enum {
	STATE_BUS_FREE=0,
	STATE_SELECT,
	STATE_RESELECT,
	STATE_CMD_DIN,
	STATE_CMD_DIN_RCV,
	STATE_CMD_DOUT,
	STATE_STATUS,
	STATE_MSGIN
};

const char *state_str[]={
	"BUS_FREE", "SELECT", "RESELECT", "CMD_DIN", "CMD_DIN_RCV", "CMD_DOUT", "STATUS", "MSGIN"
};

const char *bit_str[]={
	"O_IOPTR", "O_MSGPTR", "O_CDPTR", "O_SRAM", "O_RESET", "O_SELENA",
	"O_SCSIBSY", "O_ARB", "O_SCSIREQ", "O_SCSIMSG", "O_SCSIRST",
	"O_SCSIIO", "O_SCSICD", "O_SCSIATN", "O_SCSIACK", "O_AUTOXFR"
};

//this csr is at E0000E
//note SCSIBUF is at 0xA7000

#define IV_MSG 4
#define IV_CMD 2
#define IV_INPUT 1

static void scsi_pointer_int(int flags, int ena) {
	int v=(0x7^flags);
	emu_raise_int(INT_VECT_SCSI_POINTER|v, ena?INT_LEVEL_SCSI:0, 0);
}

void scsi_set_scsireg(scsi_t *s, unsigned int val) {
	if (val!=s->reg) {
		SCSI_LOG_DEBUG("SCSI w: reg ");
		for (int i=0; i<16; i++) {
			if (val&(1<<(15-i))) SCSI_LOG_DEBUG("%s ", bit_str[i]);
		}
		SCSI_LOG_DEBUG("\n");
	}
	int oldstate=s->state;
	if (val&O_SCSIRST) {
		//Note: SCSI RESET makes all other signals on the bus undefined and
		//tells all targets to keep their paws off the bus. The diags use this
		//to run selftests without bothering others.
		s->state=STATE_BUS_FREE;
		if (val&O_ARB) {
			//Arb succeeds immediately
			emu_raise_int(INT_VECT_SCSI_SELECTI, INT_LEVEL_SCSI, 0);
		}
		//autoxfer clears automatically when busy is low
		if ((val&O_SCSIBSY)==0) val&=~O_AUTOXFR;
		if (val&O_SCSIREQ) {
			if (s->diag&SCSI_DIAG_LATCH) SCSI_LOG_DEBUG("Diag latch on!\n");
			if (s->diag&SCSI_DIAG_PARITY) SCSI_LOG_DEBUG("Fake parity error on!\n");
			//Check flags
			int flag=val&(O_SCSIIO|O_SCSICD|O_SCSIMSG);
			int wanted_flags=0;
			if (val&O_IOPTR) wanted_flags|=O_SCSIIO;
			if (val&O_CDPTR) wanted_flags|=O_SCSICD;
			if (val&O_MSGPTR) wanted_flags|=O_SCSIMSG;
			//can't go from cd to message
			if ((s->last_req_reg&O_SCSICD) && (val&O_SCSIMSG)) wanted_flags&=~O_SCSIMSG;
			if (flag!=wanted_flags) {
				flag^=wanted_flags;
				int v=INT_VECT_SCSI_POINTER;
				if ((flag&O_SCSIMSG)==0) v|=0x4;
				if ((flag&O_SCSICD)==0) v|=0x2;
				if ((flag&O_SCSIIO)==0) v|=0x1;
				SCSI_LOG_DEBUG("SCSI: Pointer exception 0x%X. Raising interrupt.\n", flag);
				emu_raise_int(v, INT_LEVEL_SCSI, 0);
				val&=~O_AUTOXFR; //so we start without an error next time
			} else {
				if (flag&O_SCSIIO) {
					//re-use what's already on the bus... I think? (Is that what diag latch does?)
					if (s->bytecount>0) {
						if (s->ptr_read_msb) {
							s->pointer&=~1;
							s->ptr_read_msb=0;
							if (val&O_SRAM) {
								//Unsure. 8bit writes into 16bit words?
								SCSI_LOG_NOTICE("SCSI: Warning: SRAM writes are a guess...\n");
								emu_write_byte(s->pointer, 0);
								emu_write_byte(s->pointer+1, s->byte_stashed);
								emu_write_byte(s->pointer+2, 0);
								emu_write_byte(s->pointer+3, s->buf[3]);
								s->pointer+=4;
							} else {
								emu_write_byte(s->pointer, s->byte_stashed);
								emu_write_byte(s->pointer+1, s->buf[3]);
								s->pointer+=2;
							}
						} else {
							s->byte_stashed=s->buf[3];
							s->ptr_read_msb=1;
						}
					}
					val|=I_ACK;
					if (s->diag & SCSI_DIAG_PARITY) {
						emu_raise_int(INT_VECT_SCSI_PARITY, INT_LEVEL_SCSI, 0);
					}
				} else {
					s->buf[3]=emu_read_byte(s->pointer+s->ptr_read_msb);
					SCSI_LOG_DEBUG("Read %x from main memory adr %x\n", s->buf[3], s->pointer);
					//I have no idea what the 3 should come from, but that's the value the diagnostics want.
					//Later note: might have come from the buffer. ToDo: check that.
					if (s->diag & SCSI_DIAG_LATCH) s->buf[3]=3;
					if (s->bytecount>=0) {
						if (s->ptr_read_msb) {
							s->pointer&=~1;
							s->ptr_read_msb=0;
							s->pointer+=2;
						} else {
							s->ptr_read_msb=1;
						}
					}
					val|=I_ACK;
					if (s->diag & SCSI_DIAG_PARITY) {
						emu_raise_int(INT_VECT_SCSI_PARITY, INT_LEVEL_SCSI, 0);
					}
				}
				s->last_req_reg=val;
			}
			if (s->bytecount>0) s->bytecount--;
			SCSI_LOG_DEBUG("After read/write: Bytecnt %x ptr %x\n", s->bytecount, s->pointer);
		} else {
			val&=~I_ACK;
		}

/*
	Note for non-diag scsi: State is the old state, we set things up to go into the new state.
	If required, the interrupt we set up will poke the CPU after a timeout.
*/
	} else if ((val&O_ARB) && (s->state==STATE_BUS_FREE ||s->state==STATE_MSGIN)) {
		//sure, we succeeded arb.
		s->state=STATE_SELECT;
		int db=s->buf[0]&(0xff-8); //3 is our own scsi id
		for (int i=0; i<8; i++) {
			if (db&1) s->selected=i;
			db>>=1;
		}
		SCSI_LOG_INFO("Selected SCSI ID %d\n", s->selected);
		s->op_timeout_us=500;
	} else if ((val&O_SELENA) && s->state==STATE_SELECT) {
//		emu_raise_int(INT_VECT_SCSI_RESELECT, INT_LEVEL_SCSI, 0);
		s->state=STATE_RESELECT;
		s->op_timeout_us=500;
	} else if (((val&O_AUTOXFR) && (val&O_CDPTR)) && (s->state==STATE_SELECT || s->state==STATE_RESELECT)) {
//		dump_cpu_state();
//		assert(s->bytecount<=10);
		if (s->bytecount>10) s->bytecount=10;
		SCSI_LOG_INFO("SCSI CMD: ");
		for (int i=0; i<s->bytecount; i++) {
			s->cmd[i]=emu_read_byte(s->pointer++);
			SCSI_LOG_INFO("%02X ", s->cmd[i]);
		}
		SCSI_LOG_INFO("\n");
		int dir=0;
		if (s->dev[s->selected]) {
			dir=s->dev[s->selected]->handle_cmd(s->dev[s->selected], s->cmd, s->bytecount);
		}
		//put id of selected device on bus so resel works
		s->buf[2]=0; s->buf[3]=(1<<s->selected)|(1<<3);
		s->op_timeout_us=500;
		val|=I_REQ;
		if (dir==SCSI_DEV_DATA_IN) {
			val|=O_SCSIIO;
			val&=~(O_SCSICD);
			s->state=STATE_CMD_DIN;
		} else if (dir==SCSI_DEV_DATA_OUT) {
			val|=O_SCSICD;
			val&=~(O_SCSIIO);
			s->state=STATE_CMD_DOUT;
		} else {
			int status=s->dev[s->selected]->handle_status(s->dev[s->selected]);
			if (s->bytecount) {
				emu_write_byte(s->pointer++, status);
				s->bytecount--;
			}
			s->buf[2]=0; s->buf[3]=status;
			SCSI_LOG_INFO("SCSI: Device returns status %d\n", status);
			val|=O_SCSIIO|O_SCSICD;
			s->state=STATE_STATUS;
		}
		s->op_timeout_us=50;
	} else if (((val&O_AUTOXFR) && (val&O_IOPTR)) && (s->state==STATE_CMD_DIN)) {
		//Plexus has set up the pointers to receive the incoming data.
		int len=s->dev[s->selected]->handle_data_in(s->dev[s->selected], s->databuf, s->bytecount);
		SCSI_LOG_INFO("SCSI: Data from dev: ");
		for (int i=0; i<len; i++) {
			SCSI_LOG_INFO("%02X ", s->databuf[i]);
			emu_write_byte(s->pointer++, s->databuf[i]);
			s->bytecount--;
		}
		SCSI_LOG_INFO("\n");
		//Next state sets us up for status.
		val|=O_SCSICD|O_SCSIIO|O_CDPTR;
		val&=~O_IOPTR;
		s->state=STATE_CMD_DIN_RCV;
		s->op_timeout_us=50;
		int status=s->dev[s->selected]->handle_status(s->dev[s->selected]);
		s->buf[2]=0; s->buf[3]=s;
		SCSI_LOG_INFO("SCSI: Device returns status %d\n", status);
	} else if ((val&O_AUTOXFR) && (s->state==STATE_CMD_DIN_RCV)) {
		val|=O_SCSIIO|O_SCSICD|O_CDPTR;
		val&=~O_IOPTR;
		s->state=STATE_STATUS;
	} else if ((val&O_AUTOXFR) && (s->state==STATE_STATUS)) {
		s->op_timeout_us=50000;
		val|=O_SCSIIO|I_MSG;
		val&=~O_SCSICD;
//		val^=I_ACK;
		//should go to S_M_I
		val&=~I_BSY;
		s->state=STATE_MSGIN;
	} else if ((val&O_AUTOXFR) && (s->state==STATE_MSGIN)) {
		val&=~(I_REQ|I_ACK|I_IO|I_CD|I_MSG|I_BSY);
//		val|=I_ACK;
		s->buf[2]=0; s->buf[3]=0; //0=command complete
		s->state=STATE_BUS_FREE;
	} else if (s->state==STATE_BUS_FREE) {
		val&=~(I_REQ|I_ACK|I_IO|I_CD|I_MSG|I_BSY);
	}

	if (s->state==STATE_MSGIN) {
		val&=~I_BSY; 
	} else if (s->state!=STATE_BUS_FREE) {
		if (val&I_ACK) val&=~(I_REQ); else val|=I_REQ;
		val|=I_BSY; 
	} else {
		val&=~(I_REQ);
		val&=~I_BSY;
	}
	if (oldstate!=s->state) SCSI_LOG_NOTICE("Changed SCSI status %s->%s\n", state_str[oldstate], state_str[s->state]);

	s->reg=val;
	handle_interrupts(s);
}

/*
SCSI: reg O_ARB 
SCSI: reg O_ARB 
SCSI: reg O_SELENA O_SCSIBSY O_ARB 
SCSI: reg O_SELENA 
*/

unsigned int scsi_get_scsireg(scsi_t *s) {
	unsigned int ret;
	ret=s->reg;//&(O_MSGPTR|O_CDPTR|O_IOPTR|I_ACK|I_ATN|I_CD|I_IO|I_SCRST|I_MSG|I_REQ|I_BSY|I_DATEN);
	//I do not know what this field is or does. This makes diags happy though.
//	if (s->pointer!=0) ret|=I_SCZERO;

//	dump_cpu_state();
	SCSI_LOG_DEBUG("SCSI r: reg ");
	for (int i=0; i<16; i++) {
		if (ret&(1<<(15-i))) SCSI_LOG_DEBUG("%s ", bit_str[i]);
	}
	SCSI_LOG_DEBUG("\n");

	return ret;
}

/*
Note: Proper:
arbit, select, saveptrs, scrwi, loadptrs, s_s_i_int, s_m_i_int
arbit, select, scrwi, loadptrs, s_s_i_int, s_m_i_int
arbit, select, scrwi, loadptrs, s_d_i_int, saveptrs, loadptrs, s_s_i_int, s_m_i_int
arbit, select, scrwi, loadptrs, s_c_o_int, *crash*

*/

static void handle_interrupts(scsi_t *s) {
	static int old_int_to_sel=0;
	int int_to_sel=s->state;
	if (s->op_timeout_us!=0) int_to_sel=-1;
	emu_raise_int(INT_VECT_SCSI_SELECTI, (int_to_sel==STATE_SELECT)?INT_LEVEL_SCSI:0, 0);
//	emu_raise_int(INT_VECT_SCSI_RESELECT, (int_to_sel==STATE_RESELECT)?INT_LEVEL_SCSI:0, 0);
	scsi_pointer_int(IV_INPUT, (int_to_sel==STATE_CMD_DIN));
	scsi_pointer_int(0, (int_to_sel==STATE_CMD_DOUT));
	scsi_pointer_int(IV_INPUT|IV_CMD, (int_to_sel==STATE_STATUS) || (int_to_sel==STATE_CMD_DIN_RCV));
	scsi_pointer_int(IV_INPUT|IV_MSG, (int_to_sel==STATE_MSGIN));
	if (int_to_sel!=old_int_to_sel) {
		if (int_to_sel>=0) SCSI_LOG_INFO("SCSI: select int for state %s\n", state_str[int_to_sel]);
	}
	old_int_to_sel=int_to_sel;
}

void scsi_tick(scsi_t *s, int ticklen_us) {
	if (s->op_timeout_us) {
		s->op_timeout_us-=ticklen_us;
		if (s->op_timeout_us<=0) {
			s->op_timeout_us=0;
			handle_interrupts(s);
		}
	}
}


void scsi_set_diag(scsi_t *s, int flags) {
	s->diag=flags;
}

scsi_t *scsi_new() {
	scsi_t *ret=calloc(sizeof(scsi_t), 1);
	ret->state=STATE_BUS_FREE;
	return ret;
}


void scsi_add_dev(scsi_t *sc, scsi_dev_t *dev, int id) {
	sc->dev[id]=dev;
}

