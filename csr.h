
typedef struct csr_t csr_t;

unsigned int csr_read8(void *obj, unsigned int a);
unsigned int csr_read16(void *obj, unsigned int a);
void csr_write8(void *obj, unsigned int a, unsigned int val);
void csr_write16(void *obj, unsigned int a, unsigned int val);
void csr_write16_mmio(void *obj, unsigned int a, unsigned int val);
csr_t *csr_new();

int csr_cpu_is_reset(csr_t *csr, int cpu);
