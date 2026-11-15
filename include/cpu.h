#ifndef CPU16_CPU_H
#define CPU16_CPU_H

#include <stdint.h>
#include <stddef.h>
#include "isa.h"

/* Register state, counters, then memories. */
typedef struct {
    /* 16-bit data path registers */
    uint16_t acc;       /* ACC : 16-bit Accumulator                          */
    uint16_t mr;        /* MR  : 16-bit Multiply High Register               */
    uint16_t br;        /* BR  : 16-bit Buffer Register                      */
    uint16_t mbr;       /* MBR : 16-bit Memory Buffer Register               */
    uint16_t alu_out;   /* ALU : 16-bit registered ALU output                */
    int32_t  mult_reg;  /* ALU internal 32-bit signed product register       */

    /* 8-bit address & control registers */
    uint8_t  pc;        /* PC  : 8-bit Program Counter (0x00..0xFF)          */
    uint8_t  mar;       /* MAR : 8-bit Memory Address Register               */
    uint8_t  ir;        /* IR  : 8-bit Instruction Register (Opcode)         */
    uint8_t  car;       /* CAR : 8-bit Control Address Register (0x00..0x7F) */

    /* ALU status flags (matching ALU.v outputs) */
    uint8_t  carry;     /* Carry flag                                        */
    uint8_t  zero;      /* Zero flag                                         */
    uint8_t  overflow;  /* Signed overflow flag                              */
    uint8_t  sign;      /* Sign flag (ALU_out[15], connected to flag[0])     */
    uint8_t  finish;    /* Multiply finish flag (connected to flag[1])       */

    /* Execution status & performance counters */
    int      halted;
    uint64_t instr_count;
    uint64_t micro_cycles;

    /* Memories */
    uint16_t mem[CPU16_MEM_SIZE];       /* 256 x 16-bit Main Memory          */
    uint32_t cu_rom[CPU16_CU_ROM_SIZE]; /* 128 x 32-bit Microcode ROM        */
} cpu16_t;

typedef struct {
    uint64_t instr_index;
    uint8_t  pc_before;
    uint16_t instr_word;
    char     field[32];
    uint64_t expected_isa;
    uint64_t actual_micro;
} cpu16_diff_error_t;

/* Initialize CPU state, reset registers, and load default microcode ROM */
void cpu16_init(cpu16_t *cpu);

/* Reset registers and flags while preserving loaded Main Memory and CU ROM */
void cpu16_reset(cpu16_t *cpu);

/* Load 256-word main memory image */
void cpu16_load_mem(cpu16_t *cpu, const uint16_t *words, size_t count);

/* Load 128-word microprogram control ROM image */
void cpu16_load_cu_rom(cpu16_t *cpu, const uint32_t *words, size_t count);

/* Step calls: 0 = continue, 1 = HALT, -1 = error. */
int cpu16_step_micro(cpu16_t *cpu);
int cpu16_step_micro_instr(cpu16_t *cpu, uint32_t max_micro_steps);

int cpu16_step_isa(cpu16_t *cpu);

/* Run until HALT or max_instructions reached (mode: 0 = microcode, 1 = ISA) */
/* Run calls: 0 = clean HALT, -1 = error or instruction limit. */
int cpu16_run(cpu16_t *cpu, int use_isa_engine, uint64_t max_instructions);

int cpu16_run_lockstep(
    const uint16_t *mem_image,
    size_t mem_count,
    const uint32_t *cu_image,
    size_t cu_count,
    uint64_t max_instructions,
    cpu16_t *final_state_out,
    cpu16_diff_error_t *err_out
);

/* Compare one completed instruction, or step both models once.
 * step_lockstep: 0 = continue, 1 = HALT, -1 = mismatch/execution error. */
int cpu16_check_pair(const cpu16_t *isa, const cpu16_t *micro, uint64_t index,
                    uint8_t pc, uint16_t instr, cpu16_diff_error_t *err);
int cpu16_step_lockstep(cpu16_t *isa, cpu16_t *micro, cpu16_diff_error_t *err);

/* Format 32-bit seven-segment display output {MR, ACC} as 8 hex digits */
uint32_t cpu16_seven_seg_data(const cpu16_t *cpu);

#endif /* CPU16_CPU_H */
