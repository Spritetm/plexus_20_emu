#pragma once

typedef struct scsi_t scsi_t;

typedef struct scsi_dev_t scsi_dev_t;

#define SCSI_DEV_DATA_IN 0
#define SCSI_DEV_DATA_OUT 1
#define SCSI_DEV_STATUS 2
#define SCSI_DEV_ERR 3

//Interface to a SCSI device
struct scsi_dev_t {
	int (*handle_cmd)(scsi_dev_t *dev, uint8_t *cd, int len);
	int (*handle_data_in)(scsi_dev_t *dev, uint8_t *msg, int buflen);
	void (*handle_data_out)(scsi_dev_t *dev, uint8_t *msg, int len);
	int (*handle_status)(scsi_dev_t *dev);
};

//Memory range access handlers for the SCSI buffer.
unsigned int scsi_read8(void *obj, unsigned int a);
unsigned int scsi_read16(void *obj, unsigned int a);
void scsi_write8(void *obj, unsigned int a, unsigned int val);
void scsi_write16(void *obj, unsigned int a, unsigned int val);

scsi_t *scsi_new();

//Add a given SCSI device to the bus at the given SCSI ID.
void scsi_add_dev(scsi_t *s, scsi_dev_t *dev, int id);

//Communication functions for the SCSI CSR registers.
void scsi_set_bytecount(scsi_t *s, int bytecount);
void scsi_set_pointer(scsi_t *s, int pointer);
int scsi_get_bytecount(scsi_t *s);
int scsi_get_pointer(scsi_t *s);
void scsi_set_scsireg(scsi_t *s, unsigned int val);
unsigned int scsi_get_scsireg(scsi_t *s);

//Call this periodically to handle time-dependent functions
void scsi_tick(scsi_t *r, int ticklen_us);


//If the CSR SCSI diags are set, this function should be called.
#define SCSI_DIAG_LATCH 0x1
#define SCSI_DIAG_PARITY 0x2
void scsi_set_diag(scsi_t *s, int flags);

