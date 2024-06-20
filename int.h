#Interrupt vector / level definitions

#define INT_LEVEL_UART 5
#define INT_LEVEL_JOB 4
#define INT_LEVEL_DMA 2
#define INT_LEVEL_MB_IF_ERR 1
#define INT_LEVEL_SCSI 3
#define INT_LEVEL_PARITY_ERR 7
#define INT_LEVEL_CLOCK 6

//scsi errors go to the DMA cpu.
#define INT_VECT_SCSI_SPURIOUS 0x60
#define INT_VECT_SCSI_SELECTI 0x61
#define INT_VECT_SCSI_RESELECT 0x62
#define INT_VECT_SCSI_PARITY 0x64
#define INT_VECT_SCSI_POINTER 0x68 //plus message (4), command (2) input (1) flag
#define INT_VECT_PARITY_ERR 0x41
#efine INT_VECT_CLOCK 0x83
#define INT_VECT_MB_IF_ERR 0x7F
#define INT_VECT_DMA 0xc2
#define INT_VECT_JOB 0xc1

