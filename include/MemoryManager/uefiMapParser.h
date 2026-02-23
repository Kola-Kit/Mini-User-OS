#ifndef UEFI_MAP_PARSER_H
#define UEFI_MAP_PARSER_H

#include <stdint.h>

#define MAX_MEMORY_REGIONS 128

typedef struct {
    uint64_t base;      // физический адрес начала
    uint64_t pages;     // количество страниц (по 4KB)
    uint32_t type;      // тип памяти (EFI_CONVENTIONAL_MEMORY и т.д.)
    uint32_t reserved;  // выравнивание
} memory_region_t;

typedef struct {
    memory_region_t regions[MAX_MEMORY_REGIONS];
    int count;
    uint64_t total_memory;
    uint64_t free_memory;
} memory_map_t;

extern memory_map_t g_memory_map;

void ParseMemoryMap(void);

#endif