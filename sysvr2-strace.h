#ifndef SYSVR2_STRACE_H
#define SYSVR2_STRACE_H

#include <stdint.h>

uint32_t stget32(void *ctx, uint32_t addr);
uint8_t stget8(void *ctx, uint32_t addr);

char *m68k_strace(void *ctx, int d0, uint32_t sp);

#endif

