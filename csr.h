#include "scsi.h"

typedef struct csr_t csr_t;

//Read and write funtions
unsigned int csr_read8(void *obj, unsigned int a);
unsigned int csr_read16(void *obj, unsigned int a);
unsigned int csr_read32(void *obj, unsigned int a);
void csr_write8(void *obj, unsigned int a, unsigned int val);
void csr_write16(void *obj, unsigned int a, unsigned int val);
void csr_write32(void *obj, unsigned int a, unsigned int val);

//Reads/writes to the 'reset selection' region go here. Note
//that the value read/written doesn't matter; the access itself
//triggers a reset of some condition.
void csr_write16_mmio(void *obj, unsigned int a, unsigned int val);
unsigned int csr_read16_mmio(void *obj, unsigned int a);

csr_t *csr_new(scsi_t *scsi);

//Returns true if the given CPU should be kept in reset
int csr_cpu_is_reset(csr_t *csr, int cpu);

//Returns true if the clock interrupt is enabled on the given CPU
int csr_get_rtc_int_ena(csr_t *csr, int cpu);

//Returns 1 if the MBUS is held. If this is true, it also 
//sets TBUSY in the CSRs.
int csr_try_mbus_held(csr_t *csr);


//Sets the CSRs to reflect a certain access error has happened.
//Bits defined for the 'type' field:
#define ACCESS_ERROR_OK 0
#define ACCESS_ERROR_U 1
#define ACCESS_ERROR_A 2
#define ACCESS_ERROR_MBTO 4
#define ACCESS_ERROR_AJOB 8
void csr_set_access_error(csr_t *csr, int cpu, int type, int addr, int is_write);

//Sets the CSRs to reflect a parity error happened.
//Bits defined for 'hl':
#define PARITY_ERROR_H 1
#define PARITY_ERROR_L 2
void csr_set_parity_error(csr_t *c, int hl);

