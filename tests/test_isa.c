#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "isa.h"
#include "cpu.h"

/*
 * Test 1: ADD / SUB Carry, Zero, Sign, and Signed Overflow boundaries
 */
static void test_add_sub_flags(void)
{
    uint16_t mem[CPU16_MEM_SIZE] = {0};

    /*
     * Program:
     * 0x00: LOAD  0x80   (0x7FFF = +32767)
     * 0x01: ADD   0x81   (+ 0x0001 -> 0x8000 = -32768, Overflow=1, Sign=1, Carry=0)
     * 0x02: SUB   0x81   (- 0x0001 -> 0x7FFF = +32767, Overflow=1, Sign=0, Carry=0)
     * 0x03: LOAD  0x82   (0xFFFF = 65535)
     * 0x04: ADD   0x81   (+ 0x0001 -> 0x0000, Carry=1, Zero=1, Overflow=0)
     * 0x05: HALT  0x00
     */
    mem[0x00] = CPU16_MAKE_INSTR(OP_LOAD, 0x80);
    mem[0x01] = CPU16_MAKE_INSTR(OP_ADD,  0x81);
    mem[0x02] = CPU16_MAKE_INSTR(OP_SUB,  0x81);
    mem[0x03] = CPU16_MAKE_INSTR(OP_LOAD, 0x82);
    mem[0x04] = CPU16_MAKE_INSTR(OP_ADD,  0x81);
    mem[0x05] = CPU16_MAKE_INSTR(OP_HALT, 0x00);

    mem[0x80] = 0x7FFFU;
    mem[0x81] = 0x0001U;
    mem[0x82] = 0xFFFFU;

    cpu16_t cpu;
    cpu16_init(&cpu);
    cpu16_load_mem(&cpu, mem, CPU16_MEM_SIZE);

    /* Step 1: LOAD 0x7FFF */
    assert(cpu16_step_micro_instr(&cpu, 32U) == 0);
    assert(cpu.acc == 0x7FFFU);

    /* Step 2: ADD 1 -> 0x8000 (Signed positive overflow) */
    assert(cpu16_step_micro_instr(&cpu, 32U) == 0);
    assert(cpu.acc == 0x8000U);
    assert(cpu.overflow == 1U);
    assert(cpu.sign == 1U);
    assert(cpu.carry == 0U);
    assert(cpu.zero == 0U);

    /* Step 3: SUB 1 from 0x8000 -> 0x7FFF (Signed negative overflow) */
    assert(cpu16_step_micro_instr(&cpu, 32U) == 0);
    assert(cpu.acc == 0x7FFFU);
    assert(cpu.overflow == 1U);
    assert(cpu.sign == 0U);

    /* Step 4 & 5: 0xFFFF + 1 -> 0x0000 (Unsigned carry + Zero) */
    assert(cpu16_step_micro_instr(&cpu, 32U) == 0);
    assert(cpu16_step_micro_instr(&cpu, 32U) == 0);
    assert(cpu.acc == 0x0000U);
    assert(cpu.carry == 1U);
    assert(cpu.zero == 1U);
    assert(cpu.overflow == 0U);

    /* Verify full program via dual-engine lockstep */
    cpu16_t final_state;
    cpu16_diff_error_t err;
    assert(cpu16_run_lockstep(mem, CPU16_MEM_SIZE, NULL, 0, 100ULL, &final_state, &err) == 0);

    printf("PASS: test_add_sub_flags\n");
}

/*
 * Test 2: Signed 16x16 Multiplication (MPY) including negative operands and zero
 */
static void test_multiply_signed_and_zero(void)
{
    uint16_t mem[CPU16_MEM_SIZE] = {0};

    /*
     * 0x00: LOAD 0x90 (-300 = 0xFED4)
     * 0x01: MPY  0x91 (+400 = 0x0190) -> -120000 = 0xFFFE2B40 (MR=0xFFFE, ACC=0x2B40)
     * 0x02: STORE 0x92 (save low word)
     * 0x03: MPY  0x93 (* 0 -> MR=0x0000, ACC=0x0000, Zero=1)
     * 0x04: HALT 0x00
     */
    mem[0x00] = CPU16_MAKE_INSTR(OP_LOAD,  0x90);
    mem[0x01] = CPU16_MAKE_INSTR(OP_MPY,   0x91);
    mem[0x02] = CPU16_MAKE_INSTR(OP_STORE, 0x92);
    mem[0x03] = CPU16_MAKE_INSTR(OP_MPY,   0x93);
    mem[0x04] = CPU16_MAKE_INSTR(OP_HALT,  0x00);

    mem[0x90] = (uint16_t)(-300);
    mem[0x91] = 400U;
    mem[0x93] = 0U;

    cpu16_t final_state;
    cpu16_diff_error_t err;
    assert(cpu16_run_lockstep(mem, CPU16_MEM_SIZE, NULL, 0, 100ULL, &final_state, &err) == 0);

    assert(final_state.mem[0x92] == 0x2B40U);
    assert(final_state.acc == 0x0000U);
    assert(final_state.mr == 0x0000U);
    assert(final_state.zero == 1U);

    printf("PASS: test_multiply_signed_and_zero\n");
}

/*
 * Test 3: Bitwise Logical (AND, OR, NOT) and Shifts (SHL, SHR, SAR, SAL)
 */
static void test_logic_and_shifts(void)
{
    uint16_t mem[CPU16_MEM_SIZE] = {0};

    mem[0x00] = CPU16_MAKE_INSTR(OP_NOT,  0xA0); /* ~0x00FF = 0xFF00 */
    mem[0x01] = CPU16_MAKE_INSTR(OP_AND,  0xA1); /* 0xFF00 & 0x8F0F = 0x8F00 */
    mem[0x02] = CPU16_MAKE_INSTR(OP_OR,   0xA2); /* 0x8F00 | 0x0003 = 0x8F03 */
    mem[0x03] = CPU16_MAKE_INSTR(OP_SAR,  0x00); /* Arithmetic right shift: 0xC781, Carry=1 */
    mem[0x04] = CPU16_MAKE_INSTR(OP_SHR,  0x00); /* CAR 0x40 (C18 left shift): 0x8F02, Carry=1 */
    mem[0x05] = CPU16_MAKE_INSTR(OP_SHL,  0x00); /* CAR 0x48 (C19 right shift): 0x4781, Carry=0 */
    mem[0x06] = CPU16_MAKE_INSTR(OP_SAL,  0x00); /* CAR 0x78 (C19 right shift): 0x23C0, Carry=1 */
    mem[0x07] = CPU16_MAKE_INSTR(OP_HALT, 0x00);

    mem[0xA0] = 0x00FFU;
    mem[0xA1] = 0x8F0FU;
    mem[0xA2] = 0x0003U;

    cpu16_t final_state;
    cpu16_diff_error_t err;
    assert(cpu16_run_lockstep(mem, CPU16_MEM_SIZE, NULL, 0, 100ULL, &final_state, &err) == 0);
    assert(final_state.acc == 0x23C0U);
    assert(final_state.carry == 1U);

    printf("PASS: test_logic_and_shifts\n");
}

/*
 * Test 4: Unconditional JMP and conditional JMPGEZ
 */
static void test_jumps(void)
{
    uint16_t mem[CPU16_MEM_SIZE] = {0};

    mem[0x00] = CPU16_MAKE_INSTR(OP_JMP,    0x02);
    mem[0x01] = CPU16_MAKE_INSTR(OP_HALT,   0x00); /* Skipped */
    mem[0x02] = CPU16_MAKE_INSTR(OP_LOAD,   0x80); /* ACC = 1 */
    mem[0x03] = CPU16_MAKE_INSTR(OP_SUB,    0x81); /* ACC = 1 - 2 = -1 (Sign = 1) */
    mem[0x04] = CPU16_MAKE_INSTR(OP_JMPGEZ, 0x00); /* Not taken because Sign == 1 */
    mem[0x05] = CPU16_MAKE_INSTR(OP_HALT,   0x00);

    mem[0x80] = 1U;
    mem[0x81] = 2U;

    cpu16_t final_state;
    cpu16_diff_error_t err;
    assert(cpu16_run_lockstep(mem, CPU16_MEM_SIZE, NULL, 0, 100ULL, &final_state, &err) == 0);
    assert(final_state.acc == 0xFFFFU);
    assert(final_state.instr_count == 5ULL);

    printf("PASS: test_jumps\n");
}

/*
 * Test 5: Invalid opcode boundary rejection
 */
static void test_invalid_opcode(void)
{
    cpu16_t cpu;
    cpu16_init(&cpu);
    cpu.mem[0x00] = 0xFF00U; /* Invalid opcode 0xFF */

    assert(cpu16_step_isa(&cpu) == -1);
    assert(cpu.halted == 1);

    cpu16_reset(&cpu);
    assert(cpu16_step_micro_instr(&cpu, 32U) == -1);
    assert(cpu.halted == 1);

    printf("PASS: test_invalid_opcode\n");
}

int main(void)
{
    test_add_sub_flags();
    test_multiply_signed_and_zero();
    test_logic_and_shifts();
    test_jumps();
    test_invalid_opcode();

    printf("All ISA unit tests passed.\n");
    return 0;
}
