/*
 Very basic simulation of a SCSI-1 hard disk.
*/

/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <jeroen@spritesmods.com> wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return. - Sprite_tm
 * ----------------------------------------------------------------------------
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <sys/stat.h>
#include "scsi.h"
#include "emu.h"
#include "log.h"
#include "emscripten_env.h"

//Might need to change if e.g. the backing file changes for the web version.
#define COW_VERSION_MAJOR 0
#define COW_VERSION_MINOR 0

typedef struct {
	scsi_dev_t dev;
	FILE *hdfile;
	uint8_t cmd[10];
	char *cow_dir;
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

static FILE *open_cow_file(scsi_hd_t *hd, int lba, const char *mode) {
	char buf[1024];
	snprintf(buf, sizeof(buf), "%s/cow-data-%06d.bin", hd->cow_dir, lba);
	FILE *f=fopen(buf, mode);
	return f;
}

static void write_block(scsi_hd_t *hd, int lba, uint8_t *data) {
	if (hd->cow_dir) {
		FILE *f=open_cow_file(hd, lba, "w+b");
		if (!f) {
			perror("opening cow file for write");
			exit(1);
		}
		uint8_t ver[2]={COW_VERSION_MAJOR, COW_VERSION_MINOR};
		fwrite(ver, 2, 1, f);
		fwrite(data, 512, 1, f);
		fclose(f);
	} else {
		fseek(hd->hdfile, lba*512, SEEK_SET);
		fwrite(data, 512, 1, hd->hdfile);
	}
}

static void read_block(scsi_hd_t *hd, int lba, uint8_t *data) {
	if (hd->cow_dir) {
		FILE *f=open_cow_file(hd, lba, "rb");
		if (f) {
			uint8_t ver[2];
			fread(ver, 2, 1, f);
			if (ver[0]==COW_VERSION_MAJOR && ver[1]==COW_VERSION_MINOR) {
				fread(data, 512, 1, f);
				fclose(f);
				return;
			} else {
				//not the same version; ignore
				fclose(f);
			}
		}
	}
	fseek(hd->hdfile, lba*512, SEEK_SET);
	fread(data, 512, 1, hd->hdfile);
}

static int hd_handle_cmd(scsi_dev_t *dev, uint8_t *cd, int len) {
	scsi_hd_t *hd=(scsi_hd_t*)dev;
	if (len<6 || len>10) return SCSI_DEV_ERR;
	memcpy(hd->cmd, cd, len);
	if (cd[0]==0) {
		return SCSI_DEV_STATUS;
	} else if (cd[0]==1) {
		return SCSI_DEV_STATUS;
	} else if (cd[0]==3) { //sense
		return SCSI_DEV_DATA_IN;
	} else if (cd[0]==8) { //read
		return SCSI_DEV_DATA_IN;
	} else if (cd[0]==0x15) { //mode select
		return SCSI_DEV_DATA_OUT;
	} else if (cd[0]==0xa) { //write
		return SCSI_DEV_DATA_OUT;
	} else if (cd[0]==0xC2) {
		//omti config cmd?
		return SCSI_DEV_DATA_OUT;
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
		for (int i=0; i<blen/512; i++) {
			read_block(hd, lba+i, &msg[i*512]);
		}
		return blen;
	} else if (hd->cmd[0]==0xc2) {
		//omti config command?
	} else {
//		printf("Unknown command: 0x%x\n", hd->cmd[0]);
		assert(0 && "hd_handle_data_in: unknown cmd");
	}
	return 0;
}

static void hd_handle_data_out(scsi_dev_t *dev, uint8_t *msg, int len) {
	scsi_hd_t *hd=(scsi_hd_t*)dev;
	if (hd->cmd[0]==0x15) { //mode select
		//ignore
	} else if (hd->cmd[0]==0xa) { //write
		int lba=(hd->cmd[1]<<16)+(hd->cmd[2]<<8)+(hd->cmd[3]);
		int tlen=hd->cmd[4]; //note 0 means 256 blocks...
		int blen=tlen*512;
		if (blen>len) blen=len;
		for (int i=0; i<blen/512; i++) {
			write_block(hd, lba+i, &msg[i*512]);
		}
#ifdef __EMSCRIPTEN__
		emscripten_syncfs();
#endif
	}
}

static int hd_handle_status(scsi_dev_t *dev) {
	scsi_hd_t *hd=(scsi_hd_t*)dev;
	if (hd->cmd[0]==0) return 0;
	return 0; //ok
}

scsi_dev_t *scsi_dev_hd_new(const char *imagename, const char *cow_dir) {
	scsi_hd_t *hd=calloc(sizeof(scsi_hd_t), 1);
	if (cow_dir && cow_dir[0]!=0) {
		//we leave the original image intact and use copy-on-write to save
		//the new data
		hd->cow_dir=strdup(cow_dir);
		struct stat st;
		mkdir(cow_dir, 0755); //may fail, we don't care
		if (stat(cow_dir, &st) == -1) {
			perror(cow_dir);
			exit(1);
		}
		if ((st.st_mode & S_IFMT)!=S_IFDIR) {
			printf("%s: not a dir\n", cow_dir);
			exit(1);
		}
		hd->hdfile=fopen(imagename, "rb");
	} else {
		//open image r/w
		hd->hdfile=fopen(imagename, "r+b");
		hd->cow_dir=NULL;
	}
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
