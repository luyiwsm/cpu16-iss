#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "coe_loader.h"
#include "isa.h"

static void write_temp_file(const char *path, const char *content)
{
    FILE *fp = fopen(path, "wb");
    assert(fp != NULL);
    fwrite(content, 1U, strlen(content), fp);
    fclose(fp);
}

static void test_valid_coe_parsing(void)
{
    const char *path = "build/tmp_valid.coe";
    write_temp_file(
        path,
        "memory_initialization_radix=16;\n"
        "memory_initialization_vector=\n"
        "02A0, 01A4, 0700;\n"
    );

    uint16_t words[16] = {0};
    int n = coe_load_file16(path, words, 16);
    assert(n == 3);
    assert(words[0] == 0x02A0U);
    assert(words[1] == 0x01A4U);
    assert(words[2] == 0x0700U);
    remove(path);

    printf("PASS: test_valid_coe_parsing\n");
}

static void test_invalid_radix_coe(void)
{
    const char *path = "build/tmp_bad_radix.coe";
    write_temp_file(
        path,
        "memory_initialization_radix=8;\n"
        "memory_initialization_vector=02A0;\n"
    );

    uint16_t words[16] = {0};
    assert(coe_load_file16(path, words, 16) == -1);
    remove(path);

    printf("PASS: test_invalid_radix_coe\n");
}

static void test_missing_vector_coe(void)
{
    const char *path = "build/tmp_no_vec.coe";
    write_temp_file(path, "memory_initialization_radix=16;\n02A0, 0700;\n");

    uint16_t words[16] = {0};
    assert(coe_load_file16(path, words, 16) == -1);
    remove(path);

    printf("PASS: test_missing_vector_coe\n");
}

static void test_overflow_16bit_coe(void)
{
    const char *path = "build/tmp_overflow16.coe";
    write_temp_file(
        path,
        "memory_initialization_radix=16;\n"
        "memory_initialization_vector=1FFFF;\n"
    );

    uint16_t words[16] = {0};
    assert(coe_load_file16(path, words, 16) == -1);
    remove(path);

    printf("PASS: test_overflow_16bit_coe\n");
}

static void test_malformed_hex_coe(void)
{
    const char *path = "build/tmp_malformed.coe";
    write_temp_file(
        path,
        "memory_initialization_radix=16;\n"
        "memory_initialization_vector=02G0, 0700;\n"
    );

    uint16_t words[16] = {0};
    assert(coe_load_file16(path, words, 16) == -1);
    remove(path);

    printf("PASS: test_malformed_hex_coe\n");
}

static void test_asm_parser_and_coe_export(void)
{
    const char *asm_path = "build/tmp_prog.asm";
    const char *coe_path = "build/tmp_out.coe";

    write_temp_file(
        asm_path,
        "# Sample program\n"
        "LOAD 0x10\n"
        "ADD  0x11\n"
        "HALT 0x00\n"
        ".word 0x10 25\n"
        ".word 0x11 17\n"
    );

    uint16_t mem[CPU16_MEM_SIZE];
    int n = asm_load_file(asm_path, mem);
    assert(n == 5);
    assert(mem[0] == 0x0210U);
    assert(mem[1] == 0x0311U);
    assert(mem[2] == 0x0700U);
    assert(mem[0x10] == 25U);
    assert(mem[0x11] == 17U);

    assert(coe_export_file16(coe_path, mem, CPU16_MEM_SIZE) == 0);

    uint16_t reload[CPU16_MEM_SIZE] = {0};
    assert(coe_load_file16(coe_path, reload, CPU16_MEM_SIZE) == (int)CPU16_MEM_SIZE);
    assert(memcmp(mem, reload, sizeof(mem)) == 0);

    remove(asm_path);
    remove(coe_path);

    printf("PASS: test_asm_parser_and_coe_export\n");
}

static void test_asm_invalid_syntax(void)
{
    const char *asm_path = "build/tmp_bad.asm";
    write_temp_file(asm_path, "INVALID_OP 0x10\n");

    uint16_t mem[CPU16_MEM_SIZE];
    assert(asm_load_file(asm_path, mem) == -1);

    write_temp_file(asm_path, "LOAD 0x1FF\n"); /* Address > 0xFF */
    assert(asm_load_file(asm_path, mem) == -1);

    remove(asm_path);
    printf("PASS: test_asm_invalid_syntax\n");
}

int main(void)
{
    test_valid_coe_parsing();
    test_invalid_radix_coe();
    test_missing_vector_coe();
    test_overflow_16bit_coe();
    test_malformed_hex_coe();
    test_asm_parser_and_coe_export();
    test_asm_invalid_syntax();

    printf("All COE loader & assembler tests passed.\n");
    return 0;
}
