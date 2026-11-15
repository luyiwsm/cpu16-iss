#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "coe_loader.h"

static void save(const char *path, const char *text)
{
    FILE *fp = fopen(path, "w");
    assert(fp);
    assert(fputs(text, fp) >= 0);
    assert(fclose(fp) == 0);
}

static void test_parser_boundaries(void)
{
    const char *path = "build/regression.coe";
    uint16_t words[2] = {0};
    save(path, "memory_initialization_radix=2; memory_initialization_vector=101,11;");
    assert(coe_load_file16(path, words, 2) == 2 && words[0] == 5 && words[1] == 3);
    save(path, "MEMORY_INITIALIZATION_RADIX=10; MEMORY_INITIALIZATION_VECTOR=65535;");
    assert(coe_load_file16(path, words, 2) == 1 && words[0] == 65535);
    save(path, "memory_initialization_vector=1,2,3;");
    assert(coe_load_file16(path, words, 2) == -1);
    save(path, "memory_initialization_vector=1,");
    assert(coe_load_file16(path, words, 2) == -1);
    save(path, "memory_initialization_vector=-1;");
    assert(coe_load_file16(path, words, 2) == -1);
    save(path, "memory_initialization_vector=100000000000000000000000000000000000000;");
    assert(coe_load_file16(path, words, 2) == -1);
    save(path, "memory_initialization_vector=FFFFFFFF;");
    uint32_t wide[2];
    assert(coe_load_file32(path, wide, 2) == 1 && wide[0] == UINT32_MAX);
    remove(path);

    path = "build/regression.asm";
    uint16_t mem[CPU16_MEM_SIZE];
    const char *bad[] = {"LOAD\n", "NOP\n", ".wordx 0 1\n",
                         "ADD 0x80 extra\n", "HALT 0 extra\n"};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(*bad); i++) {
        save(path, bad[i]);
        assert(asm_load_file(path, mem) == -1);
    }
    save(path, "HALT\n.word 0x80 -32768\n");
    assert(asm_load_file(path, mem) == 2 && mem[0x80] == 0x8000);
    remove(path);
    puts("PASS: parser boundaries");
}

static uint32_t seed = 16;
static uint16_t random_word(void)
{
    seed = seed * 1664525u + 1013904223u;
    return (uint16_t)(seed >> 8);
}

/* Deterministic operands + boundary values, both engines, all real opcodes. */
static void test_instruction_matrix(void)
{
    const uint16_t edge[] = {0, 1, 0x7fff, 0x8000, 0xffff, 0xaaaa, 0x5555};
    for (unsigned op = OP_STORE; op <= OP_SAL; op++) {
        for (unsigned sample = 0; sample < 256; sample++) {
            cpu16_t isa, micro;
            cpu16_diff_error_t err;
            cpu16_init(&isa);
            isa.pc = sample & 1 ? 0xff : 0;
            isa.acc = sample < 49 ? edge[sample / 7] : random_word();
            isa.mem[0x80] = sample < 49 ? edge[sample % 7] : random_word();
            isa.mem[isa.pc] = CPU16_MAKE_INSTR(op, 0x80);
            isa.sign = sample & 1;
            isa.zero = (sample >> 1) & 1;
            isa.carry = (sample >> 2) & 1;
            isa.overflow = (sample >> 3) & 1;
            isa.alu_out = isa.sign ? 0x8000 : 0;
            micro = isa;
            assert(cpu16_step_lockstep(&isa, &micro, &err) == (op == OP_HALT));
        }
    }
    puts("PASS: 3840 instruction/operand cases");
}

static void test_stale_sign_and_reset(void)
{
    uint16_t mem[CPU16_MEM_SIZE] = {0};
    mem[0] = CPU16_MAKE_INSTR(OP_LOAD, 0x80);
    mem[1] = CPU16_MAKE_INSTR(OP_JMPGEZ, 3);
    mem[2] = 0xff00;  /* Must be skipped: LOAD leaves initial Sign=0. */
    mem[3] = CPU16_MAKE_INSTR(OP_HALT, 0);
    mem[0x80] = 0xffff;
    cpu16_t final;
    cpu16_diff_error_t err;
    assert(cpu16_run_lockstep(mem, CPU16_MEM_SIZE, NULL, 0, 10, &final, &err) == 0);
    assert(final.acc == 0xffff && final.sign == 0);
    uint32_t rom_word = final.cu_rom[0];
    cpu16_reset(&final);
    assert(final.pc == 0 && final.zero == 1 && final.instr_count == 0);
    assert(final.mem[0x80] == 0xffff && final.cu_rom[0] == rom_word);
    puts("PASS: stale Sign and reset semantics");
}

static void test_lockstep_diagnostics(void)
{
    cpu16_t isa, micro;
    cpu16_diff_error_t err;
    cpu16_init(&isa);
    isa.mem[0] = CPU16_MAKE_INSTR(OP_HALT, 0);
    micro = isa;
    micro.acc = 1;
    assert(cpu16_step_lockstep(&isa, &micro, &err) == -1);
    assert(!strcmp(err.field, "acc"));

    cpu16_init(&isa);
    isa.mem[0] = CPU16_MAKE_INSTR(OP_JMP, 0);
    assert(cpu16_run_lockstep(isa.mem, CPU16_MEM_SIZE, NULL, 0, 3, NULL, &err) == -1);
    assert(!strcmp(err.field, "instruction_limit"));
    isa.mem[0] = 0xff00;
    assert(cpu16_run_lockstep(isa.mem, CPU16_MEM_SIZE, NULL, 0, 3, NULL, &err) == -1);
    assert(!strcmp(err.field, "execution_error"));

    cpu16_init(&isa);
    micro = isa;
    isa.micro_cycles = UINT64_C(1) << 40;
    assert(cpu16_check_pair(&isa, &micro, 0, 0, 0, &err) == -1);
    assert(err.expected_isa == UINT64_C(1) << 40);
    puts("PASS: failure and 64-bit cycle diagnostics");
}

int main(void)
{
    test_parser_boundaries();
    test_instruction_matrix();
    test_stale_sign_and_reset();
    test_lockstep_diagnostics();
    return 0;
}
