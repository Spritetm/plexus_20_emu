
typedef struct ram_t ram_t;

//Memory range access handlers
void ram_write8(void *obj, unsigned int a, unsigned int val);
void ram_write16(void *obj, unsigned int a, unsigned int val);
void ram_write32(void *obj, unsigned int a, unsigned int val);
unsigned int ram_read8(void *obj, unsigned int a);
unsigned int ram_read16(void *obj, unsigned int a);
unsigned int ram_read32(void *obj, unsigned int a);


ram_t *rom_new(const char *filename, int size);
ram_t *ram_new(int size);

