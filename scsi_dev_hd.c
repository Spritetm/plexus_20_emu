#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "scsi.h"
#include "emu.h"
#include "log.h"

typedef struct {
	scsi_dev_t dev;
	FILE *hdfile;
	uint8_t cmd[10];
} scsi_hd_t;


static const uint8_t sense[]={
	0x80+0x00, //error code
	0, //sense key
	0,0,0, //additional information
	0, //additional sense length
	0,0,0,0, //cmd specific info
	0,	//asc
	0,	//ascq
	0,	//fru code
	0,0,0,0	//sense key specific
};

static int hd_handle_cmd(scsi_dev_t *dev, uint8_t *cd, int len) {
	scsi_hd_t *hd=(scsi_hd_t*)dev;
	if (len<6 || len>10) return SCSI_DEV_ERR;
	memcpy(hd->cmd, cd, len);
	if (cd[0]==0) {
		return SCSI_DEV_STATUS;
	} else if (cd[0]==1) {
		return SCSI_DEV_STATUS;
	} else if (cd[0]==3) {
		return SCSI_DEV_DATA_IN;
	} else if (cd[0]==8) {
		return SCSI_DEV_DATA_IN;
	} else if (cd[0]==0x15) { //mode select
		return SCSI_DEV_DATA_OUT;
	} else if (cd[0]==0xa) { //write
		return SCSI_DEV_DATA_IN;
	} else if (cd[0]==0xC2) {
		//omti config cmd?
		return SCSI_DEV_DATA_IN;
	} else {
		printf("hd: unsupported cmd %d\n", cd[0]);
		exit(1);
	}
	return SCSI_DEV_DATA_IN;
}

int hd_handle_data_in(scsi_dev_t *dev, uint8_t *msg, int buflen) {
	scsi_hd_t *hd=(scsi_hd_t*)dev;
	if (hd->cmd[0]==3) { //sense
		int clen=hd->cmd[4];
		if (clen==0) clen=4; //per scsi spec
		int lun=hd->cmd[1]>>5;
		if (clen>buflen) clen=buflen;
		memcpy(msg, sense, clen);
		return clen;
	} else if (hd->cmd[0]==8) { //read
		int lba=(hd->cmd[1]<<16)+(hd->cmd[2]<<8)+(hd->cmd[3]);
		int tlen=hd->cmd[4]; //note 0 means 256 blocks...
		int blen=tlen*512;
		if (blen>buflen) blen=buflen;
		fseek(hd->hdfile, lba*512, SEEK_SET);
		fread(msg, blen, 1, hd->hdfile);
//		printf("Read %d bytes from LB %d\n", blen, lba);
		return blen;
	} else if (hd->cmd[0]==0xc2) {
		//omti config command?
	} else {
		printf("Unknown command: 0x%x\n", hd->cmd[0]);
//		assert(0 && "hd_handle_data_in: unknown cmd");
	}
	return 0;
}

static void hd_handle_data_out(scsi_dev_t *dev, uint8_t *msg, int len) {
	scsi_hd_t *hd=(scsi_hd_t*)dev;
	if (hd->cmd[0]==0x15) { //mode select
		//ignore
	}
}

static int hd_handle_status(scsi_dev_t *dev) {
	scsi_hd_t *hd=(scsi_hd_t*)dev;
	if (hd->cmd[0]==0) return 0;
	return 0; //ok
}

scsi_dev_t *scsi_dev_hd_new(const char *imagename) {
	scsi_hd_t *hd=calloc(sizeof(scsi_hd_t), 1);
	hd->hdfile=fopen(imagename, "rb");
	if (!hd->hdfile) {
		perror(imagename);
		free(hd);
		return NULL;
	}
	hd->dev.handle_status=hd_handle_status;
	hd->dev.handle_cmd=hd_handle_cmd;
	hd->dev.handle_data_in=hd_handle_data_in;
	hd->dev.handle_data_out=hd_handle_data_out;
	return (scsi_dev_t*)hd;
}
