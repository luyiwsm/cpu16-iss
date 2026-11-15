#ifndef CPU16_DEBUGGER_H
#define CPU16_DEBUGGER_H

#include <stdio.h>
#include "cpu.h"

/* Print architectural and microarchitectural register snapshot */
void dbg_print_registers(const cpu16_t *cpu, FILE *out);

/* Print non-zero / range of main memory words */
void dbg_print_memory(const cpu16_t *cpu, uint8_t start_addr, uint8_t count, FILE *out);

/* Launch interactive CLI debugger (supports step, micro, cont, reg, mem, break, quit) */
int dbg_run_interactive(cpu16_t *cpu);

#endif /* CPU16_DEBUGGER_H */
