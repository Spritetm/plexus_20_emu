#include "scsi.h"

typedef struct csr_t csr_t;

unsigned int csr_read8(void *obj, unsigned int a);
unsigned int csr_read16(void *obj, unsigned int a);
unsigned int csr_read32(void *obj, unsigned int a);
void csr_write8(void *obj, unsigned int a, unsigned int val);
void csr_write16(void *obj, unsigned int a, unsigned int val);
void csr_write32(void *obj, unsigned int a, unsigned int val);
void csr_write16_mmio(void *obj, unsigned int a, unsigned int val);
unsigned int csr_read16_mmio(void *obj, unsigned int a);
csr_t *csr_new(scsi_t *scsi);

int csr_cpu_is_reset(csr_t *csr, int cpu);
int csr_get_rtc_int_ena(csr_t *csr, int cpu);
int csr_try_mbus_held(csr_t *csr);

#define ACCESS_ERROR_OK 0
#define ACCESS_ERROR_U 1
#define ACCESS_ERROR_A 2
#define ACCESS_ERROR_MBTO 4
#define ACCESS_ERROR_AJOB 8
void csr_set_access_error(csr_t *csr, int cpu, int type, int addr, int is_write);


void csr_set_parity_error(csr_t *c, int hl);