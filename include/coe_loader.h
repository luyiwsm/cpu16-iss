#ifndef CPU16_COE_LOADER_H
#define CPU16_COE_LOADER_H

#include <stdint.h>
#include <stddef.h>
#include "cpu.h"

int coe_load_file32(const char *path, uint32_t *out_words, size_t max_words);

int coe_load_file16(const char *path, uint16_t *out_words, size_t max_words);

int coe_export_file16(const char *path, const uint16_t *words, size_t count);

int asm_load_file(const char *path, uint16_t mem_out[CPU16_MEM_SIZE]);

int export_stats_json(const char *path, const cpu16_t *cpu, const char *mode_name, int lockstep_verified);
int export_stats_csv(const char *path, const cpu16_t *cpu, const char *mode_name, int lockstep_verified);

#endif /* CPU16_COE_LOADER_H */
