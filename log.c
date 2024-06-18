#include "log.h"
#include <stdio.h>
#include <stdarg.h>

// Default log levels for log sources
#define LOG_UART_DEFAULT_LEVEL   LOG_INFO
#define LOG_CSR_DEFAULT_LEVEL    LOG_INFO
#define LOG_MBUS_DEFAULT_LEVEL   LOG_INFO
#define LOG_MAPPER_DEFAULT_LEVEL LOG_INFO
#define LOG_SCSI_DEFAULT_LEVEL   LOG_INFO
#define LOG_EMU_DEFAULT_LEVEL    LOG_INFO

int log_channel_verbose_level[] = {
	LOG_UART_DEFAULT_LEVEL,
	LOG_CSR_DEFAULT_LEVEL,
	LOG_MBUS_DEFAULT_LEVEL,
	LOG_MAPPER_DEFAULT_LEVEL,
	LOG_SCSI_DEFAULT_LEVEL,
	LOG_EMU_DEFAULT_LEVEL
};

int log_printf(enum log_source source, int msg_level, const char *format, ...) {
        va_list ap;
        int printed = 0;

        if (log_channel_verbose_level[source] >= msg_level) {
                va_start(ap, format);
                printed = vprintf(format, ap);
                va_end(ap);
        }
        return printed;
}
