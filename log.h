#ifndef LOG_H
#define LOG_H

// Log sources (should be numbered sequentially from 0)
enum log_source {
	LOG_SRC_UART   = 0,
	LOG_SRC_CSR    = 1,
	LOG_SRC_MBUS   = 2,
	LOG_SRC_MAPPER = 3,
	LOG_SRC_SCSI   = 4,
	LOG_SRC_EMU    = 5
};


// Emulator log levels (higher is more verbose)
enum log_level {
	LOG_ERR        = 0,
	LOG_WARNING    = 1,
	LOG_NOTICE     = 2,
	LOG_INFO       = 3,
	LOG_DEBUG      = 4
};

int log_printf(enum log_source source, enum log_level msg_level, const char *format, ...);

#endif
