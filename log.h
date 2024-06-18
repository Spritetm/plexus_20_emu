#ifndef LOG_H
#define LOG_H

// Log sources (should be numbered sequentially from 0)
enum log_source {
	LOG_SRC_UART = 0,
	LOG_SRC_CSR  = 1
};


// Emulator log levels (higher is more verbose)
#define LOG_ERR        0
#define LOG_WARNING    1
#define LOG_NOTICE     2
#define LOG_INFO       3
#define LOG_DEBUG      4

int log_printf(enum log_source source, int msg_level, const char *format, ...);

#endif
