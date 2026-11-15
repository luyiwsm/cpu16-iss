#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "coe_loader.h"
#include "debugger.h"

typedef struct {
    const char *mem, *rom, *assembly, *output, *json, *csv, *mode;
    int debug, trace, verbose;
} options_t;

static void usage(const char *program)
{
    printf("Usage: %s (-a program.asm | -m memory.coe) [options]\n"
           "  -c file.coe       Control ROM (default: built-in)\n"
           "  -o file.coe       Export initial memory\n"
           "  --mode MODE       lockstep (default), micro, isa\n"
           "  --trace           Trace each instruction; keep lockstep checks\n"
           "  --debug           Interactive microcode debugger\n"
           "  --verbose         Show full registers\n"
           "  --json FILE       Export JSON summary\n"
           "  --csv FILE        Export CSV summary\n"
           "  -h, --help        Show help\n", program);
}

static int parse_options(int argc, char **argv, options_t *opt)
{
    *opt = (options_t){.mode = "lockstep"};
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        const char **value = NULL;
        if (!strcmp(arg, "-h") || !strcmp(arg, "--help")) return 1;
        if (!strcmp(arg, "--debug")) opt->debug = 1;
        else if (!strcmp(arg, "--trace")) opt->trace = 1;
        else if (!strcmp(arg, "--verbose")) opt->verbose = 1;
        else {
            if (!strcmp(arg, "-m")) value = &opt->mem;
            else if (!strcmp(arg, "-c")) value = &opt->rom;
            else if (!strcmp(arg, "-a")) value = &opt->assembly;
            else if (!strcmp(arg, "-o")) value = &opt->output;
            else if (!strcmp(arg, "--json")) value = &opt->json;
            else if (!strcmp(arg, "--csv")) value = &opt->csv;
            else if (!strcmp(arg, "--mode")) value = &opt->mode;
            if (!value || i + 1 == argc) {
                fprintf(stderr, "Unknown or incomplete option: %s\n", arg);
                return -1;
            }
            *value = argv[++i];
        }
    }
    if (!!opt->mem + !!opt->assembly != 1) {
        fprintf(stderr, "Choose exactly one input: -a or -m.\n");
        return -1;
    }
    if (strcmp(opt->mode, "lockstep") && strcmp(opt->mode, "micro") &&
        strcmp(opt->mode, "isa")) {
        fprintf(stderr, "Unknown mode: %s\n", opt->mode);
        return -1;
    }
    return 0;
}

static int file_error(const char *action, const char *path)
{
    fprintf(stderr, "Cannot %s: %s\n", action, path);
    return 1;
}

static void trace(const cpu16_t *cpu, uint8_t pc, uint16_t instr)
{
    char assembly[32];
    isa_disassemble(instr, assembly, sizeof(assembly));
    printf("%4" PRIu64 "  PC=%02X  %-12s ACC=%04X MR=%04X  cycles=%" PRIu64 "\n",
           cpu->instr_count, pc, assembly, cpu->acc, cpu->mr, cpu->micro_cycles);
}

static int execute(cpu16_t *cpu, const options_t *opt)
{
    int lockstep = !strcmp(opt->mode, "lockstep");
    int use_isa = !strcmp(opt->mode, "isa");
    cpu16_t reference = *cpu;
    for (unsigned i = 0; i < 100000; i++) {
        uint8_t pc = cpu->pc;
        uint16_t instr = cpu->mem[pc];
        cpu16_diff_error_t err;
        int rc = lockstep ? cpu16_step_lockstep(&reference, cpu, &err)
                         : use_isa ? cpu16_step_isa(cpu)
                                   : cpu16_step_micro_instr(cpu, 32);
        if (rc < 0) {
            if (lockstep)
                fprintf(stderr, "[FAIL] #%" PRIu64 " PC=%02X %s: ISA=0x%" PRIX64
                        " micro=0x%" PRIX64 "\n", err.instr_index, err.pc_before,
                        err.field, err.expected_isa, err.actual_micro);
            else fprintf(stderr, "[FAIL] Execution error at PC=%02X\n", pc);
            return -1;
        }
        if (opt->trace) trace(cpu, pc, instr);
        if (rc == 1) return 0;
    }
    fprintf(stderr, "[FAIL] Instruction limit reached without HALT (100000).\n");
    return -1;
}

int main(int argc, char **argv)
{
    options_t opt;
    int rc = parse_options(argc, argv, &opt);
    if (rc) {
        usage(argv[0]);
        return rc < 0;
    }
    cpu16_t cpu;
    cpu16_init(&cpu);
    if (opt.rom && coe_load_file32(opt.rom, cpu.cu_rom, CPU16_CU_ROM_SIZE) < 0)
        return file_error("load control ROM", opt.rom);
    if (opt.mem && coe_load_file16(opt.mem, cpu.mem, CPU16_MEM_SIZE) < 0)
        return file_error("load memory", opt.mem);
    if (opt.assembly && asm_load_file(opt.assembly, cpu.mem) < 0)
        return file_error("assemble", opt.assembly);
    if (opt.output && coe_export_file16(opt.output, cpu.mem, CPU16_MEM_SIZE))
        return file_error("export memory", opt.output);
    if (opt.debug) return dbg_run_interactive(&cpu) != 0;
    if (execute(&cpu, &opt)) return 1;

    int verified = !strcmp(opt.mode, "lockstep");
    if (opt.json && export_stats_json(opt.json, &cpu, opt.mode, verified))
        return file_error("write JSON", opt.json);
    if (opt.csv && export_stats_csv(opt.csv, &cpu, opt.mode, verified))
        return file_error("write CSV", opt.csv);
    printf("[PASS] %s\n", verified ? "Lockstep verified" : "Halted");
    printf("ACC = %d (0x%04X)  MR = 0x%04X\n", (int16_t)cpu.acc, cpu.acc, cpu.mr);
    printf("Instructions = %" PRIu64 "  Cycles = %" PRIu64 "  CPI = %.2f\n",
           cpu.instr_count, cpu.micro_cycles,
           cpu.instr_count ? (double)cpu.micro_cycles / cpu.instr_count : 0);
    if (opt.verbose) dbg_print_registers(&cpu, stdout);
    return 0;
}
