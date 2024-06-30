
//Memory range read/write handlers for the Multibus memory range
void mbus_write8(void *obj, unsigned int a, unsigned int val);
void mbus_write16(void *obj, unsigned int a, unsigned int val);
void mbus_write32(void *obj, unsigned int a, unsigned int val);
unsigned int mbus_read8(void *obj, unsigned int a);
unsigned int mbus_read16(void *obj, unsigned int a);
unsigned int mbus_read32(void *obj, unsigned int a);

//Memory range read/write handlers for the Multibus IO range
void mbus_io_write(void *obj, unsigned int a, unsigned int val);
unsigned int mbus_io_read(void *obj, unsigned int a);
