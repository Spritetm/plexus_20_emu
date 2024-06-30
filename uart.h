

typedef struct uart_t uart_t;

//Memory range access handlers
void uart_write8(void *obj, unsigned int addr, unsigned int val);
unsigned int uart_read8(void *obj, unsigned int addr);

uart_t *uart_new(const char *name, int is_console);

//Call this periodically to handle timed events
void uart_tick(uart_t *u, int ticklen_us);
