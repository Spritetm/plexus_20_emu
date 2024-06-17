
typedef struct scsi_t scsi_t;

unsigned int scsi_read8(void *obj, unsigned int a);
unsigned int scsi_read16(void *obj, unsigned int a);
void scsi_write8(void *obj, unsigned int a, unsigned int val);
void scsi_write16(void *obj, unsigned int a, unsigned int val);
scsi_t *scsi_new();

void scsi_set_bytecount(scsi_t *s, int bytecount);
void scsi_set_pointer(scsi_t *s, int pointer);
int scsi_get_bytecount(scsi_t *s);
int scsi_get_pointer(scsi_t *s);
void scsi_set_scsireg(scsi_t *s, unsigned int val);
unsigned int scsi_get_scsireg(scsi_t *s);

#define SCSI_DIAG_LATCH 0x1
#define SCSI_DIAG_PARITY 0x2
void scsi_set_diag(scsi_t *s, int flags);

