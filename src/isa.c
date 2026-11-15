#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "isa.h"

static int str_ieq(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return 0;
        }
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

static const uint32_t DEFAULT_CU_ROM[CPU16_CU_ROM_SIZE] = {
    /* 00 Fetch */ 0x00000401UL, 0x00000009UL, 0x00000011UL, 0x00000002UL, 0x00000002UL, 0x00000002UL, 0x00000002UL, 0x00000002UL,
    /* 08 LOAD */ 0x00000061UL, 0x00000009UL, 0x02000004UL, 0x00000002UL, 0x00000002UL, 0x00000002UL, 0x00000002UL, 0x00000002UL,
    /* 10 STORE */ 0x00000061UL, 0x00000801UL, 0x00001004UL, 0x00000002UL, 0x00000002UL, 0x00000002UL, 0x00000002UL, 0x00000002UL,
    /* 18 ADD */ 0x00000061UL, 0x00000009UL, 0x00000081UL, 0x00600001UL, 0x00000201UL, 0x00800004UL, 0x00000002UL, 0x00000002UL,
    /* 20 SUB */ 0x00000061UL, 0x00000009UL, 0x00000081UL, 0x00600001UL, 0x00002001UL, 0x00800004UL, 0x00000002UL, 0x00000002UL,
    /* 28 AND */ 0x00000061UL, 0x00000009UL, 0x00000081UL, 0x00600001UL, 0x00008001UL, 0x00800004UL, 0x00000002UL, 0x00000002UL,
    /* 30 OR */ 0x00000061UL, 0x00000009UL, 0x00000081UL, 0x00600001UL, 0x00010001UL, 0x00800004UL, 0x00000002UL, 0x00000002UL,
    /* 38 NOT */ 0x00000061UL, 0x00000009UL, 0x02000001UL, 0x00400001UL, 0x00020001UL, 0x00800004UL, 0x00000002UL, 0x00000002UL,
    /* 40 SHR (left) */ 0x00400041UL, 0x00040001UL, 0x00800004UL, 0x00000002UL, 0x00000002UL, 0x00000002UL, 0x00000002UL, 0x00000002UL,
    /* 48 SHL (right) */ 0x00400041UL, 0x00080001UL, 0x00800004UL, 0x00000002UL, 0x00000002UL, 0x00000002UL, 0x00000002UL, 0x00000002UL,
    /* 50 MPY */ 0x00000061UL, 0x00000009UL, 0x00000081UL, 0x00600001UL, 0x0C000000UL, 0x01000001UL, 0x00800004UL, 0x00000002UL,
    /* 58 JMP */ 0x00004004UL, 0x00000002UL, 0x00000002UL, 0x00000002UL, 0x00000002UL, 0x00000002UL, 0x00000002UL, 0x00000002UL,
    /* 60 JMPGEZ */ 0x00100000UL, 0x00004004UL, 0x00000044UL, 0x00000002UL, 0x00000002UL, 0x00000002UL, 0x00000002UL, 0x00000002UL,
    /* 68 HALT */ 0x00000004UL, 0x00000002UL, 0x00000002UL, 0x00000002UL, 0x00000002UL, 0x00000002UL, 0x00000002UL, 0x00000002UL,
    /* 70 SAR */ 0x00400041UL, 0x10000001UL, 0x00800004UL, 0x00000002UL, 0x00000002UL, 0x00000002UL, 0x00000002UL, 0x00000002UL,
    /* 78 SAL (right) */ 0x00400041UL, 0x00080001UL, 0x00800004UL, 0x00000002UL, 0x00000002UL, 0x00000002UL, 0x00000002UL, 0x00000002UL
};


/* Index is the instruction opcode. NOP is named for display, not executable. */
static const struct { const char *name; uint8_t entry; } instructions[] = {
    {"NOP", 0x00}, {"STORE", 0x10}, {"LOAD", 0x08}, {"ADD", 0x18},
    {"SUB", 0x20}, {"JMPGEZ", 0x60}, {"JMP", 0x58}, {"HALT", 0x68},
    {"MPY", 0x50}, {"AND", 0x28}, {"OR", 0x30}, {"NOT", 0x38},
    {"SHL", 0x48}, {"SHR", 0x40}, {"SAR", 0x70}, {"SAL", 0x78}
};

const char *isa_opcode_name(uint8_t op)
{
    return op <= OP_SAL ? instructions[op].name : "UNKNOWN";
}

int isa_parse_mnemonic(const char *name)
{
    if (!name) return -1;
    for (int op = 0; op <= OP_SAL; op++)
        if (str_ieq(name, instructions[op].name)) return op;
    return -1;
}

uint8_t isa_micro_entry(uint8_t op)
{
    return op <= OP_SAL ? instructions[op].entry : 0;
}

void isa_disassemble(uint16_t instr, char *buf, size_t size)
{
    if (!buf || !size) return;
    uint8_t op = CPU16_OPCODE(instr);
    if (op == OP_HALT || op == OP_NOP)
        snprintf(buf, size, "%s", isa_opcode_name(op));
    else snprintf(buf, size, "%s 0x%02X", isa_opcode_name(op), CPU16_ADDR(instr));
}

void isa_format_control_word(uint32_t word, char *buf, size_t size)
{
    if (!buf || !size) return;
    size_t used = 0;
    buf[0] = '\0';
    for (unsigned bit = 0; bit < CPU16_NUM_SIGNALS; bit++) {
        if (!(word & (1u << bit))) continue;
        int n = snprintf(buf + used, size - used, "%sC%u", used ? "," : "", bit);
        if (n < 0 || (size_t)n >= size - used) return;
        used += (size_t)n;
    }
    if (!used) snprintf(buf, size, "NONE");
}

void isa_load_default_cu_rom(uint32_t rom[CPU16_CU_ROM_SIZE])
{
    if (rom) memcpy(rom, DEFAULT_CU_ROM, sizeof(DEFAULT_CU_ROM));
}
