#ifndef LOG_H
#define LOG_H

// Log sources (should be numbered sequentially from 0)
// If you change the order log_channel_verbose_level in log.c needs updating too
enum log_source {
	LOG_SRC_UART = 0,
	LOG_SRC_CSR,
	LOG_SRC_MBUS,
	LOG_SRC_MAPPER,
	LOG_SRC_SCSI,
	LOG_SRC_RAMROM,
	LOG_SRC_RTC,
	LOG_SRC_EMU,
	LOG_SRC_MAX //end sentinel, leave at end of enum
};


// Emulator log levels (higher is more verbose)
enum log_level {
	LOG_ERR = 0,
	LOG_WARNING,
	LOG_NOTICE,
	LOG_INFO,
	LOG_DEBUG,
	LOG_LVL_MAX //end sentinel, leave at end of enum
};

void log_set_level(enum log_source source, enum log_level msg_level);
int log_printf(enum log_source source, enum log_level msg_level, const char *format, ...);
int log_level_active(enum log_source source, enum log_level msg_level);

#endif
