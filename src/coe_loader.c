#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "coe_loader.h"

/* A COE token is a number/name or one of '=', ',' and ';'. */
static int token(FILE *fp, char text[64])
{
    int c;
    do c = fgetc(fp); while (c != EOF && isspace((unsigned char)c));
    if (c == EOF) return ferror(fp) ? -1 : 0;
    size_t n = 0;
    if (c == '=' || c == ',' || c == ';') text[n++] = (char)c;
    else {
        do {
            if (n == 63) return -1;
            text[n++] = (char)tolower((unsigned char)c);
            c = fgetc(fp);
        } while (c != EOF && !isspace((unsigned char)c) && c != '=' && c != ',' && c != ';');
        if (c != EOF) ungetc(c, fp);
    }
    text[n] = '\0';
    return 1;
}

static int expect(FILE *fp, const char *wanted)
{
    char text[64];
    return token(fp, text) == 1 && !strcmp(text, wanted);
}

static int integer(const char *text, int base, int64_t low, int64_t high, int64_t *out)
{
    char *end;
    errno = 0;
    long long value = strtoll(text, &end, base);
    if (!*text || *end || errno || value < low || value > high) return -1;
    *out = value;
    return 0;
}

static int load_coe(const char *path, void *words, size_t capacity, int bits)
{
    if (!path || !words || !capacity) return -1;
    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;
    int result = -1, radix = 16;
    char text[64];
    size_t count = 0;
    long size;
    if (fseek(fp, 0, SEEK_END) || (size = ftell(fp)) <= 0 ||
        size > 1024 * 1024 || fseek(fp, 0, SEEK_SET)) goto done;

    if (token(fp, text) != 1) goto done;
    if (!strcmp(text, "memory_initialization_radix")) {
        int64_t value;
        if (!expect(fp, "=") || token(fp, text) != 1 ||
            integer(text, 10, 2, 16, &value) ||
            (value != 2 && value != 10 && value != 16) || !expect(fp, ";")) goto done;
        radix = (int)value;
        if (token(fp, text) != 1) goto done;
    }
    if (strcmp(text, "memory_initialization_vector") || !expect(fp, "=")) goto done;
    for (;;) {
        int64_t value;
        if (token(fp, text) != 1 || count == capacity ||
            integer(text, radix, 0, bits == 16 ? UINT16_MAX : UINT32_MAX, &value)) goto done;
        if (bits == 16) ((uint16_t *)words)[count] = (uint16_t)value;
        else ((uint32_t *)words)[count] = (uint32_t)value;
        count++;
        int next = token(fp, text);
        if (next < 0) goto done;
        /* Supplied Vivado memory images omit the final semicolon. */
        if (!next) { result = (int)count; goto done; }
        if (!strcmp(text, ";")) break;
        if (strcmp(text, ",")) goto done;
    }
    if (token(fp, text) != 0) goto done;
    result = (int)count;
done:
    fclose(fp);
    return result;
}

int coe_load_file32(const char *path, uint32_t *words, size_t capacity)
{
    return load_coe(path, words, capacity, 32);
}

int coe_load_file16(const char *path, uint16_t *words, size_t capacity)
{
    return load_coe(path, words, capacity, 16);
}

static int close_output(FILE *fp)
{
    int failed = ferror(fp);
    if (fclose(fp)) failed = 1;
    return failed ? -1 : 0;
}

int coe_export_file16(const char *path, const uint16_t *words, size_t count)
{
    if (!path || !words || !count) return -1;
    FILE *fp = fopen(path, "w");
    if (!fp) return -1;
    fputs("memory_initialization_radix=16;\nmemory_initialization_vector=\n", fp);
    for (size_t i = 0; i < count; i++)
        fprintf(fp, "%04X%s", words[i], i + 1 == count ? ";\n" :
                i % 16 == 15 ? ",\n" : ", ");
    return close_output(fp);
}

int asm_load_file(const char *path, uint16_t mem[CPU16_MEM_SIZE])
{
    if (!path || !mem) return -1;
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;
    char line[256], name[64], arg[64], value[64], extra[64];
    unsigned pc = 0;
    int count = 0, failed = 0;
    memset(mem, 0, CPU16_MEM_SIZE * sizeof(*mem));
    while (fgets(line, sizeof(line), fp)) {
        if (!strchr(line, '\n') && !feof(fp)) { failed = 1; break; }
        line[strcspn(line, "#;")] = '\0';
        int n = sscanf(line, "%63s %63s %63s %63s", name, arg, value, extra);
        if (n < 1) continue;
        for (char *p = name; *p; p++) *p = (char)tolower((unsigned char)*p);
        int64_t address = 0, data;
        if (!strcmp(name, ".word")) {
            if (n != 3 || integer(arg, 0, 0, 255, &address) ||
                integer(value, 0, -32768, 65535, &data)) { failed = 1; break; }
            mem[address] = (uint16_t)data;
        } else {
            int op = isa_parse_mnemonic(name);
            int no_address = op == OP_HALT || (op >= OP_SHL && op <= OP_SAL);
            if (op < OP_STORE || op > OP_SAL || pc == CPU16_MEM_SIZE ||
                n > 2 || (!no_address && n != 2) ||
                (n == 2 && integer(arg, 0, 0, 255, &address))) { failed = 1; break; }
            mem[pc++] = CPU16_MAKE_INSTR(op, address);
        }
        count++;
    }
    if (ferror(fp)) failed = 1;
    fclose(fp);
    return failed || !count ? -1 : count;
}

int export_stats_json(const char *path, const cpu16_t *cpu, const char *mode, int verified)
{
    if (!path || !cpu) return -1;
    /* Mode is a fixed CLI enum, never arbitrary JSON text. */
    if (!mode) mode = "microcode";
    if (strcmp(mode, "lockstep") && strcmp(mode, "micro") &&
        strcmp(mode, "isa") && strcmp(mode, "microcode")) return -1;
    FILE *fp = fopen(path, "w");
    if (!fp) return -1;
    fprintf(fp,
        "{\n  \"engine_mode\": \"%s\",\n  \"lockstep_verified\": %s,\n"
        "  \"halted\": %s,\n  \"instructions_executed\": %" PRIu64 ",\n"
        "  \"micro_cycles\": %" PRIu64 ",\n  \"average_cpi\": %.3f,\n"
        "  \"registers\": {\n    \"ACC_hex\": \"0x%04X\",\n"
        "    \"ACC_unsigned\": %u,\n    \"ACC_signed\": %d,\n"
        "    \"MR_hex\": \"0x%04X\",\n    \"seven_seg_hex\": \"0x%08X\",\n"
        "    \"PC\": \"0x%02X\",\n    \"IR\": \"0x%02X\",\n"
        "    \"MAR\": \"0x%02X\",\n    \"MBR\": \"0x%04X\",\n"
        "    \"BR\": \"0x%04X\",\n    \"CAR\": \"0x%02X\"\n  },\n"
        "  \"flags\": {\n    \"Carry\": %u,\n    \"Zero\": %u,\n"
        "    \"Overflow\": %u,\n    \"Sign\": %u\n  }\n}\n",
        mode, verified ? "true" : "false", cpu->halted ? "true" : "false",
        cpu->instr_count, cpu->micro_cycles,
        cpu->instr_count ? (double)cpu->micro_cycles / cpu->instr_count : 0,
        cpu->acc, cpu->acc, (int16_t)cpu->acc, cpu->mr, cpu16_seven_seg_data(cpu),
        cpu->pc, cpu->ir, cpu->mar, cpu->mbr, cpu->br, cpu->car,
        cpu->carry, cpu->zero, cpu->overflow, cpu->sign);
    return close_output(fp);
}

int export_stats_csv(const char *path, const cpu16_t *cpu, const char *mode, int verified)
{
    if (!path || !cpu) return -1;
    FILE *fp = fopen(path, "w");
    if (!fp) return -1;
    fputs("engine_mode,lockstep_verified,halted,instructions,micro_cycles,cpi,"
          "ACC_hex,ACC_signed,MR_hex,seven_seg_hex,PC_hex,Carry,Zero,Overflow,Sign\n", fp);
    fprintf(fp, "%s,%d,%d,%" PRIu64 ",%" PRIu64 ",%.3f,0x%04X,%d,0x%04X,"
            "0x%08X,0x%02X,%u,%u,%u,%u\n", mode ? mode : "microcode",
            !!verified, !!cpu->halted, cpu->instr_count, cpu->micro_cycles,
            cpu->instr_count ? (double)cpu->micro_cycles / cpu->instr_count : 0,
            cpu->acc, (int16_t)cpu->acc, cpu->mr, cpu16_seven_seg_data(cpu),
            cpu->pc, cpu->carry, cpu->zero, cpu->overflow, cpu->sign);
    return close_output(fp);
}
