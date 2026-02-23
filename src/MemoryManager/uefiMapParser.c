#include <efi.h>
#include <efilib.h>

#include "MemoryManager/uefiMapParser.h"
#include "kernel.h"

memory_map_t g_memory_map = {0};

void ParseMemoryMap(void) {    
    if (mmp.map == NULL) { return; }
    if (mmp.mapSize == 0) { return; }
    if (mmp.descSize < sizeof(EFI_MEMORY_DESCRIPTOR)) { return; }

    uint64_t desc_counter = 0;

    for (; desc_counter < mmp.mapSize / mmp.descSize; desc_counter++) {
        EFI_MEMORY_DESCRIPTOR *desc = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)mmp.map + (desc_counter * mmp.descSize));

        memory_region_t res = {desc->PhysicalStart, desc->NumberOfPages, desc->Type, 0};

        g_memory_map.total_memory += desc->NumberOfPages * 4096;

        if (desc->Type == EfiConventionalMemory) {
            g_memory_map.free_memory += desc->NumberOfPages * 4096;
        }

        g_memory_map.regions[g_memory_map.count] = res;
        g_memory_map.count++;
    }
}