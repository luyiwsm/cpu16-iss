#include <stdio.h>
#include <string.h>
#include "cpu.h"

void cpu16_init(cpu16_t *cpu)
{
    if (!cpu) return;
    memset(cpu, 0, sizeof(*cpu));
    cpu->zero = 1;
    isa_load_default_cu_rom(cpu->cu_rom);
}

void cpu16_reset(cpu16_t *cpu)
{
    if (!cpu) return;
    /* Memories are at the end of cpu16_t; reset only the register state. */
    memset(cpu, 0, offsetof(cpu16_t, mem));
    cpu->zero = 1;
}

void cpu16_load_mem(cpu16_t *cpu, const uint16_t *words, size_t count)
{
    if (!cpu || !words) return;
    if (count > CPU16_MEM_SIZE) count = CPU16_MEM_SIZE;
    memset(cpu->mem, 0, sizeof(cpu->mem));
    memcpy(cpu->mem, words, count * sizeof(*words));
}

void cpu16_load_cu_rom(cpu16_t *cpu, const uint32_t *words, size_t count)
{
    if (!cpu || !words) return;
    if (count > CPU16_CU_ROM_SIZE) count = CPU16_CU_ROM_SIZE;
    memset(cpu->cu_rom, 0, sizeof(cpu->cu_rom));
    memcpy(cpu->cu_rom, words, count * sizeof(*words));
}

/* A small helper reads one control signal without changing CPU state. */
static unsigned bit(uint32_t word, unsigned index)
{
    return (word >> index) & 1;
}

/* Used only inside the micro engine, never by the instruction model. */
static void micro_logic_flags(cpu16_t *cpu, uint16_t result, unsigned carry)
{
    cpu->zero = result == 0;
    cpu->overflow = 0;
    cpu->carry = (uint8_t)carry;
}

int cpu16_step_micro(cpu16_t *cpu)
{
    if (!cpu) return -1;
    if (cpu->halted) return 1;
    uint8_t car = cpu->car & 0x7f;
    uint32_t cw = cpu->cu_rom[car];
    cpu->micro_cycles++;
    if (car == 0x68) {
        cpu->car = 0;
        cpu->instr_count++;
        cpu->halted = 1;
        return 1;
    }

    /* Every right-hand register read uses the state before this clock edge. */
    uint8_t next_pc = cpu->pc, next_mar = cpu->mar;
    uint8_t next_ir = cpu->ir, next_car = cpu->car;
    uint16_t next_mbr = cpu->mbr, next_br = cpu->br;
    uint16_t next_acc = cpu->acc, next_mr = cpu->mr, result = cpu->alu_out;
    uint16_t a = cpu->acc, b = cpu->br;

    /* Two-bit muxes: 0/3 hold, 1 selects the first input, 2 the second. */
    switch ((bit(cw, 10) << 1) | bit(cw, 5)) {
    case 1: next_mar = (uint8_t)cpu->mbr; break;
    case 2: next_mar = cpu->pc; break;
    }
    switch ((bit(cw, 11) << 1) | bit(cw, 3)) {
    case 1: next_mbr = cpu->mem[cpu->mar]; break;
    case 2: next_mbr = a; break;
    }
    switch ((bit(cw, 14) << 1) | bit(cw, 6)) {
    case 1: next_pc = (uint8_t)(cpu->pc + 1); break;
    case 2: next_pc = (uint8_t)cpu->mbr; break;
    }
    if (cw & CU_SIG_C12_MEM_WRITE) cpu->mem[cpu->mar] = cpu->mbr;
    if (cw & CU_SIG_C4_IR_FROM_MBR) next_ir = cpu->mbr >> 8;
    if (cw & CU_SIG_C7_BR_FROM_MBR) next_br = cpu->mbr;
    /* ACC controls have ordered priority: MBR > ALU > clear. */
    if (cw & CU_SIG_C8_ACC_CLEAR) next_acc = 0;
    if (cw & CU_SIG_C23_ACC_FROM_L) next_acc = cpu->alu_out;
    if (cw & CU_SIG_C25_ACC_FROM_M) next_acc = cpu->mbr;

    /* Preserve the original ALU priority, including pass-through before SAR. */
    if (cw & CU_SIG_C9_ALU_ADD) {
        uint32_t sum = (uint32_t)a + b;
        result = (uint16_t)sum;
        cpu->carry = bit(sum, 16);
        cpu->zero = result == 0;
        cpu->overflow = (bit(a, 15) == bit(b, 15)) &&
                        (bit(result, 15) != bit(a, 15));
    } else if (cw & CU_SIG_C13_ALU_SUB) {
        uint32_t diff = (uint32_t)a - b;
        result = (uint16_t)diff;
        cpu->carry = bit(diff, 16);
        cpu->zero = result == 0;
        cpu->overflow = (bit(a, 15) != bit(b, 15)) &&
                        (bit(result, 15) != bit(a, 15));
    } else if (cw & CU_SIG_C15_ALU_AND) {
        result = a & b;
        micro_logic_flags(cpu, result, 0);
    } else if (cw & CU_SIG_C16_ALU_OR) {
        result = a | b;
        micro_logic_flags(cpu, result, 0);
    } else if (cw & CU_SIG_C17_ALU_NOT) {
        result = (uint16_t)~a;
        micro_logic_flags(cpu, result, 0);
    } else if (cw & CU_SIG_C18_ALU_SHL) {
        result = (uint16_t)(a << 1);
        micro_logic_flags(cpu, result, a >> 15);
    } else if (cw & CU_SIG_C19_ALU_SHR) {
        result = a >> 1;
        micro_logic_flags(cpu, result, a & 1);
    } else if (cw & CU_SIG_C24_ALU_MUL_WB) {
        uint32_t product = (uint32_t)cpu->mult_reg;
        result = (uint16_t)product;
        next_mr = (uint16_t)(product >> 16);
        cpu->zero = product == 0;
    } else if (cw & CU_SIG_C26_ALU_MUL_ST) {
        cpu->mult_reg = (int32_t)(int16_t)a * (int32_t)(int16_t)b;
        cpu->finish = 1;
        cpu->carry = cpu->overflow = 0;
        cpu->zero = cpu->mult_reg == 0;
        if (!a || !b) result = 0;
    } else if (cw & CU_SIG_C21_ALU_PASS_B) {
        result = b;
    } else if (cw & CU_SIG_C22_ALU_PASS_A) {
        result = a;
    } else if (cw & CU_SIG_C28_ALU_SAR) {
        result = (a & 0x8000) | (a >> 1);
        micro_logic_flags(cpu, result, a >> 15);
    }

    unsigned select = (bit(cw, 27) << 4) | (bit(cw, 20) << 3) |
                      (bit(cw, 2) << 2) | (bit(cw, 1) << 1) | bit(cw, 0);
    /* Branch sees the old Sign; multiply wait sees the updated Finish. */
    switch (select) {
    case 1: next_car = (uint8_t)(cpu->car + 1); break;
    case 2:
        next_car = isa_micro_entry(cpu->ir);
        if (!next_car) { cpu->halted = 1; return -1; }
        break;
    case 4:
        next_car = 0;
        cpu->instr_count++;
        break;
    case 8: next_car = (uint8_t)(cpu->car + 1 + (cpu->sign & 1)); break;
    case 16: next_car = (uint8_t)(cpu->car + (cpu->finish & 1)); break;
    }

    cpu->pc = next_pc;
    cpu->mar = next_mar;
    cpu->mbr = next_mbr;
    cpu->ir = next_ir;
    cpu->br = next_br;
    cpu->acc = next_acc;
    cpu->mr = next_mr;
    cpu->alu_out = result;
    cpu->sign = result >> 15;
    cpu->car = next_car;
    return 0;
}

int cpu16_step_micro_instr(cpu16_t *cpu, uint32_t limit)
{
    if (!cpu) return -1;
    if (!limit) limit = 32;
    uint64_t start = cpu->instr_count;
    for (uint32_t i = 0; i < limit; i++) {
        int rc = cpu16_step_micro(cpu);
        if (rc) return rc;
        if (cpu->instr_count > start && cpu->car == 0) return 0;
    }
    return -1;
}

/* Instruction model: deliberately independent of the micro engine's ALU. */
int cpu16_step_isa(cpu16_t *cpu)
{
    static const unsigned cycles[] = {0, 7, 7, 10, 10, 6, 5, 5,
                                      11, 10, 10, 10, 7, 7, 7, 7};
    if (!cpu) return -1;
    if (cpu->halted) return 1;

    uint16_t instr = cpu->mem[cpu->pc];
    uint8_t op = CPU16_OPCODE(instr), addr = CPU16_ADDR(instr);
    uint16_t a = cpu->acc, b = cpu->mem[addr], result = 0;
    int writes_alu = 0;
    cpu->mar = cpu->pc;
    cpu->mbr = instr;
    cpu->ir = op;
    cpu->car = 0;
    if (op < OP_STORE || op > OP_SAL) {
        cpu->halted = 1;
        return -1;
    }

    /* All sequential instructions advance PC; jumps and HALT handle it below. */
    if (op != OP_JMP && op != OP_JMPGEZ && op != OP_HALT)
        cpu->pc++;
    if (op == OP_STORE || op == OP_LOAD || op == OP_ADD ||
        op == OP_SUB || op == OP_MPY || op == OP_AND ||
        op == OP_OR || op == OP_NOT) {
        cpu->mar = addr;
        cpu->mbr = b;
    }
    if (op == OP_ADD || op == OP_SUB || op == OP_MPY ||
        op == OP_AND || op == OP_OR)
        cpu->br = b;

    switch (op) {
    case OP_STORE:
        cpu->mbr = a;
        cpu->mem[addr] = a;
        break;
    case OP_LOAD:
        cpu->acc = b;  /* LOAD does not refresh ALU flags. */
        break;
    case OP_ADD: {
        uint32_t sum = (uint32_t)a + b;
        result = (uint16_t)sum;
        cpu->carry = (sum >> 16) & 1;
        cpu->overflow = ((~(a ^ b) & (a ^ result)) >> 15) & 1;
        writes_alu = 1;
        break;
    }
    case OP_SUB: {
        uint32_t diff = (uint32_t)a - b;
        result = (uint16_t)diff;
        cpu->carry = (diff >> 16) & 1;
        cpu->overflow = (((a ^ b) & (a ^ result)) >> 15) & 1;
        writes_alu = 1;
        break;
    }
    case OP_JMPGEZ:
        cpu->pc = cpu->sign ? (uint8_t)(cpu->pc + 1) : addr;
        break;
    case OP_JMP:
        cpu->pc = addr;
        break;
    case OP_HALT:
        cpu->halted = 1;
        break;
    case OP_MPY: {
        cpu->mult_reg = (int32_t)(int16_t)a * (int32_t)(int16_t)b;
        uint32_t product = (uint32_t)cpu->mult_reg;
        result = (uint16_t)product;
        cpu->mr = (uint16_t)(product >> 16);
        cpu->finish = 1;
        writes_alu = 1;
        break;
    }
    case OP_AND: result = a & b; writes_alu = 1; break;
    case OP_OR:  result = a | b; writes_alu = 1; break;
    case OP_NOT: result = (uint16_t)~b; writes_alu = 1; break;
    /* Preserve the supplied ROM's historical mnemonic/direction mapping. */
    case OP_SHR:
        result = (uint16_t)(a << 1);
        cpu->carry = a >> 15;
        writes_alu = 1;
        break;
    case OP_SHL:
    case OP_SAL:
        result = a >> 1;
        cpu->carry = a & 1;
        writes_alu = 1;
        break;
    case OP_SAR:
        result = (a & 0x8000) | (a >> 1);
        cpu->carry = a >> 15;  /* Original SAR carry semantics. */
        writes_alu = 1;
        break;
    }

    if (writes_alu) {
        cpu->acc = cpu->alu_out = result;
        cpu->sign = result >> 15;
        cpu->zero = op == OP_MPY ? cpu->mult_reg == 0 : result == 0;
        if (op != OP_ADD && op != OP_SUB) cpu->overflow = 0;
        if (op == OP_MPY || op == OP_AND || op == OP_OR || op == OP_NOT)
            cpu->carry = 0;
    }
    cpu->instr_count++;
    cpu->micro_cycles += cycles[op];
    return cpu->halted ? 1 : 0;
}

int cpu16_run(cpu16_t *cpu, int use_isa, uint64_t limit)
{
    if (!cpu) return -1;
    if (!limit) limit = 100000;
    for (uint64_t i = 0; i < limit; i++) {
        int rc = use_isa ? cpu16_step_isa(cpu) : cpu16_step_micro_instr(cpu, 32);
        if (rc) return rc == 1 ? 0 : -1;
    }
    return -1;
}

static int record_diff(cpu16_diff_error_t *err, uint64_t index, uint8_t pc,
                       uint16_t instr, const char *field,
                       uint64_t expected, uint64_t actual)
{
    if (err) {
        err->instr_index = index;
        err->pc_before = pc;
        err->instr_word = instr;
        snprintf(err->field, sizeof(err->field), "%s", field);
        err->expected_isa = expected;
        err->actual_micro = actual;
    }
    return -1;
}

int cpu16_check_pair(const cpu16_t *isa, const cpu16_t *micro, uint64_t index,
                     uint8_t pc, uint16_t instr, cpu16_diff_error_t *err)
{
    if (!isa || !micro) return -1;
    /* One comparison rule for every scalar; arithmetic remains independent. */
#define CHECK(field) \
    if (isa->field != micro->field) \
        return record_diff(err, index, pc, instr, #field, isa->field, micro->field)
    CHECK(pc);
    CHECK(acc);
    CHECK(mr);
    CHECK(carry);
    CHECK(zero);
    CHECK(overflow);
    CHECK(sign);
    CHECK(micro_cycles);
    CHECK(instr_count);
    CHECK(halted);
#undef CHECK
    for (size_t i = 0; i < CPU16_MEM_SIZE; i++) {
        if (isa->mem[i] != micro->mem[i]) {
            char field[32];
            snprintf(field, sizeof(field), "MEM[0x%02X]", (unsigned)i);
            return record_diff(err, index, pc, instr, field, isa->mem[i], micro->mem[i]);
        }
    }
    return 0;
}

/* Shared by batch execution and traced execution: trace never bypasses checks. */
int cpu16_step_lockstep(cpu16_t *isa, cpu16_t *micro, cpu16_diff_error_t *err)
{
    if (!isa || !micro) return -1;
    uint64_t index = isa->instr_count;
    uint8_t pc = isa->pc;
    uint16_t instr = isa->mem[pc];
    if (err) memset(err, 0, sizeof(*err));
    int a = cpu16_step_isa(isa);
    int b = cpu16_step_micro_instr(micro, 32);
    if (a != b || a < 0)
        return record_diff(err, index, pc, instr,
                           a == b ? "execution_error" : "return_code",
                           (uint64_t)(int64_t)a, (uint64_t)(int64_t)b);
    if (cpu16_check_pair(isa, micro, index, pc, instr, err)) return -1;
    return a;
}

int cpu16_run_lockstep(const uint16_t *mem, size_t mem_count,
                       const uint32_t *rom, size_t rom_count, uint64_t limit,
                       cpu16_t *final, cpu16_diff_error_t *err)
{
    if (err) memset(err, 0, sizeof(*err));
    if (!mem) return record_diff(err, 0, 0, 0, "invalid_input", 0, 0);
    cpu16_t isa, micro;
    cpu16_init(&isa);
    cpu16_load_mem(&isa, mem, mem_count);
    if (rom && rom_count) cpu16_load_cu_rom(&isa, rom, rom_count);
    micro = isa;
    if (!limit) limit = 100000;
    for (uint64_t i = 0; i < limit; i++) {
        int rc = cpu16_step_lockstep(&isa, &micro, err);
        if (rc < 0) return -1;
        if (rc == 1) {
            if (final) *final = micro;
            return 0;
        }
    }
    return record_diff(err, limit, isa.pc, isa.mem[isa.pc], "instruction_limit", limit, limit);
}

uint32_t cpu16_seven_seg_data(const cpu16_t *cpu)
{
    return cpu ? ((uint32_t)cpu->mr << 16) | cpu->acc : 0;
}
