#ifndef SAVE_MEMORY_MAP_H
#define SAVE_MEMORY_MAP_H

#include "efi.h"
#include "efilib.h"

typedef struct {
    EFI_STATUS status;
    UINTN mapSize;
    EFI_MEMORY_DESCRIPTOR *map;
    UINTN mapKey;
    UINTN descSize;
    UINT32 descVersion;
} MemoryMap;

MemoryMap SaveMemoryMap(void);

#endif