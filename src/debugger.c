#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "debugger.h"

void dbg_print_registers(const cpu16_t *cpu, FILE *out)
{
    if (!cpu || !out) return;
    char next[32];
    isa_disassemble(cpu->mem[cpu->pc], next, sizeof(next));
    fprintf(out, "PC=%02X IR=%02X CAR=%02X  next: %s\n"
            "ACC=%04X (%d) MR=%04X BR=%04X ALU=%04X\n"
            "MAR=%02X MBR=%04X  C=%u Z=%u V=%u S=%u F=%u halted=%d\n"
            "instructions=%" PRIu64 " cycles=%" PRIu64 " display=%08X\n",
            cpu->pc, cpu->ir, cpu->car, next, cpu->acc, (int16_t)cpu->acc,
            cpu->mr, cpu->br, cpu->alu_out, cpu->mar, cpu->mbr,
            cpu->carry, cpu->zero, cpu->overflow, cpu->sign, cpu->finish,
            cpu->halted, cpu->instr_count, cpu->micro_cycles, cpu16_seven_seg_data(cpu));
}

void dbg_print_memory(const cpu16_t *cpu, uint8_t start, uint8_t count, FILE *out)
{
    if (!cpu || !out) return;
    unsigned end = (unsigned)start + (count ? count : 16);
    for (unsigned i = start; i < end && i < CPU16_MEM_SIZE; i++) {
        char assembly[32];
        isa_disassemble(cpu->mem[i], assembly, sizeof(assembly));
        fprintf(out, "%02X: %04X  %6d  %s\n",
                i, cpu->mem[i], (int16_t)cpu->mem[i], assembly);
    }
}

static int number(const char *text, unsigned *value)
{
    char *end;
    errno = 0;
    unsigned long n = strtoul(text, &end, 0);
    if (!*text || *text == '-' || *end || errno || n > 255) return -1;
    *value = (unsigned)n;
    return 0;
}

int dbg_run_interactive(cpu16_t *cpu)
{
    if (!cpu) return -1;
    unsigned char breaks[CPU16_MEM_SIZE] = {0};
    char line[128], cmd[32], arg[32], size[32];
    puts("s: instruction  m: microcycle  c: continue  r: registers\n"
         "x addr [count]: memory  b addr: breakpoint  q: quit");
    dbg_print_registers(cpu, stdout);
    for (;;) {
        printf("cpu16> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;
        int n = sscanf(line, "%31s %31s %31s", cmd, arg, size);
        if (n < 1) continue;
        if (!strcmp(cmd, "q") || !strcmp(cmd, "quit")) break;
        if (!strcmp(cmd, "r") || !strcmp(cmd, "reg")) {
            dbg_print_registers(cpu, stdout);
            continue;
        }
        if (!strcmp(cmd, "b") || !strcmp(cmd, "break") ||
            !strcmp(cmd, "x") || !strcmp(cmd, "mem")) {
            unsigned addr = 0, count = 8;
            int breakpoint = cmd[0] == 'b';
            if ((breakpoint && n < 2) || (n >= 2 && number(arg, &addr)) ||
                (n >= 3 && number(size, &count))) {
                puts("Invalid address/count (use 0..255).");
                continue;
            }
            if (breakpoint) {
                breaks[addr] = 1;
                printf("Breakpoint at %02X\n", addr);
            } else dbg_print_memory(cpu, (uint8_t)addr, (uint8_t)count, stdout);
            continue;
        }
        int rc;
        if (!strcmp(cmd, "m") || !strcmp(cmd, "micro")) {
            unsigned car = cpu->car & 0x7f;
            uint32_t word = cpu->cu_rom[car];
            char signals[128];
            isa_format_control_word(word, signals, sizeof(signals));
            rc = cpu16_step_micro(cpu);
            printf("CAR=%02X CW=%08X [%s] -> CAR=%02X\n", car, word, signals, cpu->car);
        } else if (!strcmp(cmd, "s") || !strcmp(cmd, "step")) {
            rc = cpu16_step_micro_instr(cpu, 32);
        } else if (!strcmp(cmd, "c") || !strcmp(cmd, "cont")) {
            /* Finish the current instruction before checking the next PC. */
            unsigned steps = 0;
            do {
                rc = cpu16_step_micro_instr(cpu, 32);
                steps++;
            } while (!rc && !breaks[cpu->pc] && steps < 100000);
            if (!rc) puts(breaks[cpu->pc] ? "Breakpoint reached." : "Instruction limit reached.");
        } else {
            puts("Unknown command. Use s, m, c, r, x, b or q.");
            continue;
        }
        if (rc < 0) puts("Execution error or microcycle limit reached.");
        if (rc == 1) puts("Halted.");
        dbg_print_registers(cpu, stdout);
    }
    return 0;
}
