#include "log.h"
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>

// Default log levels for log sources
#define LOG_UART_DEFAULT_LEVEL   LOG_INFO
#define LOG_CSR_DEFAULT_LEVEL    LOG_INFO
#define LOG_MBUS_DEFAULT_LEVEL   LOG_INFO
#define LOG_MAPPER_DEFAULT_LEVEL LOG_INFO
#define LOG_SCSI_DEFAULT_LEVEL   LOG_INFO
#define LOG_EMU_DEFAULT_LEVEL    LOG_INFO
#define LOG_RAMROM_DEFAULT_LEVEL LOG_INFO
#define LOG_RTC_DEFAULT_LEVEL    LOG_INFO
#define LOG_EMU_DEFAULT_LEVEL    LOG_INFO

int log_channel_verbose_level[] = {
	[LOG_SRC_UART]=LOG_UART_DEFAULT_LEVEL,
	[LOG_SRC_CSR]=LOG_CSR_DEFAULT_LEVEL,
	[LOG_SRC_MBUS]=LOG_MBUS_DEFAULT_LEVEL,
	[LOG_SRC_MAPPER]=LOG_MAPPER_DEFAULT_LEVEL,
	[LOG_SRC_SCSI]=LOG_SCSI_DEFAULT_LEVEL,
	[LOG_SRC_RAMROM]=LOG_RAMROM_DEFAULT_LEVEL,
	[LOG_SRC_RTC]=LOG_RTC_DEFAULT_LEVEL,
	[LOG_SRC_EMU]=LOG_EMU_DEFAULT_LEVEL
};

// "ANSI" colour escape sequences
#define ANSI_COLOUR_NORMAL       "\033[0m"
#define ANSI_COLOUR_RED          "\033[31m"
#define ANSI_COLOUR_GREEN        "\033[32m"
#define ANSI_COLOUR_YELLOW       "\033[33m"
#define ANSI_COLOUR_MAGENTA      "\033[35m"
#define ANSI_COLOUR_GREY         "\033[37m"

// These must be in teh same order as enum log_level (in log.h)
const char *log_level_colour[] = {
	ANSI_COLOUR_RED,     // LOG_ERR     (0)
	ANSI_COLOUR_MAGENTA, // LOG_WARNING (1)
	ANSI_COLOUR_YELLOW,  // LOG_NOTICE  (2)
	ANSI_COLOUR_GREEN,   // LOG_INFO    (3)
	ANSI_COLOUR_GREY     // LOG_DEBUG   (4)
};

void log_set_level(enum log_source source, enum log_level msg_level) {
	log_channel_verbose_level[source]=msg_level;
}

int log_printf(enum log_source source, enum log_level msg_level, const char *format, ...) {
	static_assert(sizeof(log_channel_verbose_level)/sizeof(log_channel_verbose_level[0])==LOG_SRC_MAX, 
				"log_channel_verbose_level missing an entry");
	va_list ap;
	int printed = 0;

	if (log_channel_verbose_level[source] >= msg_level) {
		printf("%s", log_level_colour[msg_level]);
		va_start(ap, format);
		printed = vprintf(format, ap);
		va_end(ap);
		printf("%s", ANSI_COLOUR_NORMAL);
	}
	return printed;
}
