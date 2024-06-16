
typedef struct mapper_t mapper_t;

unsigned int mapper_read16(void *obj, unsigned int a);
void mapper_write16(void *obj, unsigned int a, unsigned int val);
unsigned int mapper_read32(void *obj, unsigned int a);
void mapper_write32(void *obj, unsigned int a, unsigned int val);
mapper_t *mapper_new(void *physram, int size);

void mapper_ram_write8(void *obj, unsigned int a, unsigned int val);
void mapper_ram_write16(void *obj, unsigned int a, unsigned int val);
void mapper_ram_write32(void *obj, unsigned int a, unsigned int val);
unsigned int mapper_ram_read8(void *obj, unsigned int a);
unsigned int mapper_ram_read16(void *obj, unsigned int a);
unsigned int mapper_ram_read32(void *obj, unsigned int a);

void mapper_set_sysmode(mapper_t *m, int cpu_in_sysmode);
