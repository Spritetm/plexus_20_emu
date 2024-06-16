
typedef struct scsi_t scsi_t;

unsigned int scsi_read8(void *obj, unsigned int a);
unsigned int scsi_read16(void *obj, unsigned int a);
void scsi_write8(void *obj, unsigned int a, unsigned int val);
void scsi_write16(void *obj, unsigned int a, unsigned int val);
scsi_t *scsi_new();

void scsi_set_bytecount(scsi_t *s, int bytecount);
void scsi_set_pointer(scsi_t *s, int pointer);
void scsi_set_scsireg(scsi_t *s, unsigned int val);
unsigned int scsi_get_scsireg(scsi_t *s);

