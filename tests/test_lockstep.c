#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "cpu.h"
#include "coe_loader.h"

/*
 * End-to-end Test 1: Vivado blk_cu.coe + blk_mem.coe (Sum 1..100 = 5050)
 */
static void test_vivado_blk_mem_sum100(void)
{
    uint32_t cu_rom[CPU16_CU_ROM_SIZE] = {0};
    uint16_t mem[CPU16_MEM_SIZE] = {0};

    int n_cu = coe_load_file32("rom/blk_cu.coe", cu_rom, CPU16_CU_ROM_SIZE);
    int n_mem = coe_load_file16("rom/blk_mem.coe", mem, CPU16_MEM_SIZE);
    assert(n_cu == 128);
    assert(n_mem > 0);

    cpu16_t final_cpu;
    cpu16_diff_error_t err;
    int rc = cpu16_run_lockstep(
        mem,
        CPU16_MEM_SIZE,
        cu_rom,
        CPU16_CU_ROM_SIZE,
        10000ULL,
        &final_cpu,
        &err
    );

    assert(rc == 0);
    assert(final_cpu.halted == 1);
    assert(final_cpu.acc == 5050U); /* 0x13BA */
    assert(final_cpu.mr == 0x0000U);
    assert(final_cpu.mem[0xA4] == 5050U);
    assert(final_cpu.instr_count == 713ULL);
    assert(final_cpu.micro_cycles == 5494ULL);
    assert(cpu16_seven_seg_data(&final_cpu) == 0x000013BAUL);

    printf("PASS: test_vivado_blk_mem_sum100 (ACC=0x%04X [5050], Instrs=713, MicroCycles=5494)\n",
           final_cpu.acc);
}

/*
 * End-to-end Test 2: Vivado TEST_SUM.COE (Compact Sum 1..100 = 5050)
 */
static void test_vivado_test_sum_coe(void)
{
    uint16_t mem[CPU16_MEM_SIZE] = {0};
    int n_mem = coe_load_file16("rom/TEST_SUM.COE", mem, CPU16_MEM_SIZE);
    assert(n_mem > 0);

    cpu16_t final_cpu;
    cpu16_diff_error_t err;
    int rc = cpu16_run_lockstep(mem, CPU16_MEM_SIZE, NULL, 0, 10000ULL, &final_cpu, &err);

    assert(rc == 0);
    assert(final_cpu.halted == 1);
    assert(final_cpu.acc == 5050U);
    assert(final_cpu.mem[0x0C] == 5050U);
    assert(final_cpu.instr_count == 709ULL);
    assert(final_cpu.micro_cycles == 5466ULL);

    printf("PASS: test_vivado_test_sum_coe  (ACC=0x%04X [5050], Instrs=709, MicroCycles=5466)\n",
           final_cpu.acc);
}

/*
 * End-to-end Test 3: Vivado TEST.coe (MPY + 3x SAR + OR + AND + STORE/LOAD)
 */
static void test_vivado_test_coe_Composite(void)
{
    uint16_t mem[CPU16_MEM_SIZE] = {0};
    int n_mem = coe_load_file16("rom/TEST.coe", mem, CPU16_MEM_SIZE);
    assert(n_mem > 0);

    cpu16_t final_cpu;
    cpu16_diff_error_t err;
    int rc = cpu16_run_lockstep(mem, CPU16_MEM_SIZE, NULL, 0, 1000ULL, &final_cpu, &err);

    assert(rc == 0);
    assert(final_cpu.halted == 1);
    assert(final_cpu.acc == 0x70E0U);
    assert(final_cpu.mr == 0x1661U);
    assert(final_cpu.mem[0xA2] == 0x70E0U);
    assert(final_cpu.instr_count == 10ULL);
    assert(final_cpu.micro_cycles == 78ULL);

    printf("PASS: test_vivado_test_coe      (ACC=0x%04X, MR=0x%04X, Instrs=10, MicroCycles=78)\n",
           final_cpu.acc, final_cpu.mr);
}

/*
 * End-to-end Test 4: Assembled rom/sum_100.asm lockstep verification
 */
static void test_assembled_sum_100_asm(void)
{
    uint16_t mem[CPU16_MEM_SIZE] = {0};
    assert(asm_load_file("rom/sum_100.asm", mem) > 0);

    cpu16_t final_cpu;
    cpu16_diff_error_t err;
    assert(cpu16_run_lockstep(mem, CPU16_MEM_SIZE, NULL, 0, 10000ULL, &final_cpu, &err) == 0);
    assert(final_cpu.acc == 5050U);
    assert(final_cpu.instr_count == 713ULL);
    assert(final_cpu.micro_cycles == 5494ULL);

    printf("PASS: test_assembled_sum_100_asm\n");
}

int main(void)
{
    test_vivado_blk_mem_sum100();
    test_vivado_test_sum_coe();
    test_vivado_test_coe_Composite();
    test_assembled_sum_100_asm();

    printf("All Lockstep differential verification tests passed.\n");
    return 0;
}
