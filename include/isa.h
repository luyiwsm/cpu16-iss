#ifndef CPU16_ISA_H
#define CPU16_ISA_H

#include <stdint.h>
#include <stddef.h>

#define CPU16_MEM_SIZE    256U
#define CPU16_CU_ROM_SIZE 128U
#define CPU16_NUM_SIGNALS 29U

#define CPU16_OPCODE(instr)  ((uint8_t)(((instr) >> 8) & 0xFFU))
#define CPU16_ADDR(instr)    ((uint8_t)((instr) & 0xFFU))
#define CPU16_MAKE_INSTR(op, addr) ((uint16_t)((((uint16_t)(op) & 0xFFU) << 8) | ((uint16_t)(addr) & 0xFFU)))

typedef enum {
    OP_NOP    = 0x00,
    OP_STORE  = 0x01, /* CAR -> 0x10 : MEM[addr] <= ACC                    */
    OP_LOAD   = 0x02, /* CAR -> 0x08 : ACC <= MEM[addr]                    */
    OP_ADD    = 0x03, /* CAR -> 0x18 : ACC <= ACC + MEM[addr]              */
    OP_SUB    = 0x04, /* CAR -> 0x20 : ACC <= ACC - MEM[addr]              */
    OP_JMPGEZ = 0x05, /* CAR -> 0x60 : if (Sign == 0) PC <= addr           */
    OP_JMP    = 0x06, /* CAR -> 0x58 : PC <= addr                          */
    OP_HALT   = 0x07, /* CAR -> 0x68 : Halt processor                      */
    OP_MPY    = 0x08, /* CAR -> 0x50 : {MR, ACC} <= signed(ACC)*MEM[addr]  */
    OP_AND    = 0x09, /* CAR -> 0x28 : ACC <= ACC & MEM[addr]              */
    OP_OR     = 0x0A, /* CAR -> 0x30 : ACC <= ACC | MEM[addr]              */
    OP_NOT    = 0x0B, /* CAR -> 0x38 : ACC <= ~MEM[addr]                   */
    OP_SHL    = 0x0C, /* CAR -> 0x48 : Shift instruction (CAR 0x48)        */
    OP_SHR    = 0x0D, /* CAR -> 0x40 : Shift instruction (CAR 0x40)        */
    OP_SAR    = 0x0E, /* CAR -> 0x70 : ACC <= {ACC[15], ACC[15:1]}         */
    OP_SAL    = 0x0F  /* CAR -> 0x78 : Shift instruction (CAR 0x78)        */
} cpu16_opcode_t;

#define CU_SIG_C0_CAR_INC     (1UL << 0)  /* CAR <= CAR + 1                     */
#define CU_SIG_C1_CAR_DECODE  (1UL << 1)  /* CAR <= opcode_entry(IR)            */
#define CU_SIG_C2_CAR_RESET   (1UL << 2)  /* CAR <= 0 (Return to Fetch)         */
#define CU_SIG_C3_MBR_FROM_M  (1UL << 3)  /* MBR <= MEM[MAR]                    */
#define CU_SIG_C4_IR_FROM_MBR (1UL << 4)  /* IR  <= MBR[15:8]                   */
#define CU_SIG_C5_MAR_FROM_MB (1UL << 5)  /* MAR <= MBR[7:0]                    */
#define CU_SIG_C6_PC_INC      (1UL << 6)  /* PC  <= PC + 1                      */
#define CU_SIG_C7_BR_FROM_MBR (1UL << 7)  /* BR  <= MBR                         */
#define CU_SIG_C8_ACC_CLEAR   (1UL << 8)  /* ACC <= 0                           */
#define CU_SIG_C9_ALU_ADD     (1UL << 9)  /* ALU: {Carry, result} = ACC + BR    */
#define CU_SIG_C10_MAR_FROM_P (1UL << 10) /* MAR <= PC                          */
#define CU_SIG_C11_MBR_FROM_A (1UL << 11) /* MBR <= ACC                         */
#define CU_SIG_C12_MEM_WRITE  (1UL << 12) /* MEM[MAR] <= MBR (wea = 1)          */
#define CU_SIG_C13_ALU_SUB    (1UL << 13) /* ALU: {Carry, result} = ACC - BR    */
#define CU_SIG_C14_PC_FROM_MB (1UL << 14) /* PC  <= MBR[7:0]                    */
#define CU_SIG_C15_ALU_AND    (1UL << 15) /* ALU: result = ACC & BR             */
#define CU_SIG_C16_ALU_OR     (1UL << 16) /* ALU: result = ACC | BR             */
#define CU_SIG_C17_ALU_NOT    (1UL << 17) /* ALU: result = ~ACC                 */
#define CU_SIG_C18_ALU_SHL    (1UL << 18) /* ALU: result = {ACC[14:0], 1'b0}    */
#define CU_SIG_C19_ALU_SHR    (1UL << 19) /* ALU: result = {1'b0, ACC[15:1]}    */
#define CU_SIG_C20_CAR_JMPGEZ (1UL << 20) /* CAR <= CAR + 1 + Sign              */
#define CU_SIG_C21_ALU_PASS_B (1UL << 21) /* ALU: result <= BR                  */
#define CU_SIG_C22_ALU_PASS_A (1UL << 22) /* ALU: result <= ACC                 */
#define CU_SIG_C23_ACC_FROM_L (1UL << 23) /* ACC <= ALU_out                     */
#define CU_SIG_C24_ALU_MUL_WB (1UL << 24) /* ALU: result <= mult[15:0], MR<=hi  */
#define CU_SIG_C25_ACC_FROM_M (1UL << 25) /* ACC <= MBR                         */
#define CU_SIG_C26_ALU_MUL_ST (1UL << 26) /* ALU: mult = signed(ACC)*signed(BR) */
#define CU_SIG_C27_CAR_MUL_WT (1UL << 27) /* CAR <= CAR + finish                */
#define CU_SIG_C28_ALU_SAR    (1UL << 28) /* ALU: result = {ACC[15], ACC[15:1]} */

/* Return mnemonic string for an opcode (e.g., "ADD", "MPY", "JMPGEZ") */
const char *isa_opcode_name(uint8_t opcode);

/* Look up opcode value from mnemonic string (case-insensitive); returns -1 if unknown */
int isa_parse_mnemonic(const char *mnemonic);

/* Return microprogram entry address in CAR ROM for a given opcode (CAR.v mapping) */
uint8_t isa_micro_entry(uint8_t opcode);

/* Format a 16-bit instruction word into human-readable assembly */
void isa_disassemble(uint16_t instr, char *buf, size_t buf_size);

/* Format active control signals C0..C28 of a 32-bit microinstruction word */
void isa_format_control_word(uint32_t cw, char *buf, size_t buf_size);

/* Load the default built-in blk_cu.coe microprogram ROM table (128 x 32-bit) */
void isa_load_default_cu_rom(uint32_t cu_rom[CPU16_CU_ROM_SIZE]);

#endif /* CPU16_ISA_H */
