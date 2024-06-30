#include "sysvr2-strace.h"
#include <stdbool.h>
#include <stdio.h>

static char stbuf[1024];
static int stbuflen;

static void stputc(char c)
{
    if (stbuflen < sizeof(stbuf) - 1) {
        stbuf[stbuflen++] = c;
    }
}

static void stprintstr(void *ctx, uint32_t p)
{
    stputc('\'');
    while (true) {
        char c = stget8(ctx, p);
        if (c == 0) {
            break;
        }
        stputc(c);
        p++;
    }
    stputc('\'');
}

static void stprintnum(const char *fmt, int val)
{
    char buf[12];
    char *p = buf;
    snprintf(buf, sizeof(buf), fmt, val);
    while (*p) {
        stputc(*p);
        p++;
    }
}

static void stprint(void *ctx, uint32_t sp, const char *fmt, ...)
{
    const char *p = fmt;
    bool in_fmt = false;
    uint32_t arg;
    uint32_t ptr;

    while (true) {
        char c = *(p++);
        if (c == 0) {
            break;
        }
        if (in_fmt) {
            switch (c) {
            case 's':
                arg = stget32(ctx, sp);
                sp += 4;
                stprintstr(ctx, arg);
                break;
            case 'd':
                arg = stget32(ctx, sp);
                sp += 4;
                stprintnum("%d", arg);
                break;
            case 'p':
                arg = stget32(ctx, sp);
                sp += 4;
                stprintnum("%06x", arg);
                break;
            case 'r':
                arg = stget32(ctx, sp);
                sp += 4;
                c = 0;
                while (true) {
                    ptr = stget32(ctx, arg);
                    if (!ptr) {
                        break;
                    }
                    if (c) {
                        stputc(c);
                    }
                    stprintstr(ctx, ptr);
                    arg += 4;
                    c = ',';
                }
                break;
            default:
                stputc(c);
                break;
            }
            in_fmt = false;
        } else if (c == '%') {
            in_fmt = true;
        } else {
            stputc(c);
        }
    }
}
char *m68k_strace(void *ctx, int d0, uint32_t sp)
{
    stbuflen = 0;
    sp += 4;
    switch (d0) {
    case 1:
        stprint(ctx, sp, "rexit");
        break;
    case 2:
        stprint(ctx, sp, "fork");
        break;
    case 3:
        stprint(ctx, sp, "read");
        break;
    case 4:
        stprint(ctx, sp, "write");
        break;
    case 5:
        stprint(ctx, sp, "open %s");
        break;
    case 6:
        stprint(ctx, sp, "close");
        break;
    case 7:
        stprint(ctx, sp, "wait");
        break;
    case 11:
        stprint(ctx, sp, "exec %s args:%p");
        break;
    case 12:
        stprint(ctx, sp, "chdir %s");
        break;
    case 13:
        stprint(ctx, sp, "gtime");
        break;
    case 16:
        stprint(ctx, sp, "chown");
        break;
    case 17:
        stprint(ctx, sp, "sbreak");
        break;
    case 18:
        stprint(ctx, sp, "stat %s");
        break;
    case 19:
        stprint(ctx, sp, "seek");
        break;
    case 20:
        stprint(ctx, sp, "getpid");
        break;
    case 23:
        stprint(ctx, sp, "setuid");
        break;
    case 24:
        stprint(ctx, sp, "getuid");
        break;
    case 27:
        stprint(ctx, sp, "alarm");
        break;
    case 28:
        stprint(ctx, sp, "fstat");
        break;
    case 29:
        stprint(ctx, sp, "pause");
        break;
    case 36:
        stprint(ctx, sp, "sync");
        break;
    case 39:
        stprint(ctx, sp, "setpgrp");
        break;
    case 41:
        stprint(ctx, sp, "dup");
        break;
    case 46:
        stprint(ctx, sp, "setgid");
        break;
    case 48:
        stprint(ctx, sp, "ssig");
        break;
    case 54:
        stprint(ctx, sp, "ioctl");
        break;
    case 59:
        stprint(ctx, sp, "exec %s args:%r env:%r");
        break;
    default:
        stprint(ctx, sp, "syscall ");
        stprintnum("%d", d0);
        break;
    }
    stbuf[stbuflen] = 0;
    return stbuf;
}

