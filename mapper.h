#include "ramrom.h"


typedef struct mapper_t mapper_t;

//Read/write functions for the mapper page table RAMs.
unsigned int mapper_read8(void *obj, unsigned int a);
void mapper_write8(void *obj, unsigned int a, unsigned int val);
unsigned int mapper_read16(void *obj, unsigned int a);
void mapper_write16(void *obj, unsigned int a, unsigned int val);
unsigned int mapper_read32(void *obj, unsigned int a);
void mapper_write32(void *obj, unsigned int a, unsigned int val);

mapper_t *mapper_new(ram_t *physram, int size, int yolo);

//Read/write functions for the main RAM address space.
void mapper_ram_write8(void *obj, unsigned int a, unsigned int val);
void mapper_ram_write16(void *obj, unsigned int a, unsigned int val);
void mapper_ram_write32(void *obj, unsigned int a, unsigned int val);
unsigned int mapper_ram_read8(void *obj, unsigned int a);
unsigned int mapper_ram_read16(void *obj, unsigned int a);
unsigned int mapper_ram_read32(void *obj, unsigned int a);

//Select if the current CPU is in system mode or in user mode.
void mapper_set_sysmode(mapper_t *m, int cpu_in_sysmode);
//Set the active map ID.
void mapper_set_mapid(mapper_t *m, uint8_t id);

//note RWX flags match page tables
#define ACCESS_SYSTEM 0x1
#define ACCESS_R 0x80000000
#define ACCESS_W 0x40000000
#define ACCESS_X 0x20000000

//Check if an access allowed at the given address for the given access.
//Returns one of ACCESS_ERROR_x.
int mapper_access_allowed(mapper_t *m, unsigned int a, int access_flags);


