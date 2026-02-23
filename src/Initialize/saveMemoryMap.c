#include <efi.h>
#include <efilib.h>

#include "Initialize/saveMemoryMap.h"
#include "kernel.h"

MemoryMap SaveMemoryMap(void) {
    if (!systemIsNotEBS) {
        MemoryMap result = {EFI_NOT_READY, 0, NULL, 0, 0, 0};
        return result;
    }

    EFI_STATUS status;
    UINTN mapSize = 0;
    EFI_MEMORY_DESCRIPTOR *map = NULL;
    UINTN mapKey = 0;
    UINTN descSize = 0;
    UINT32 descVersion = 0;

    status = uefi_call_wrapper(BS->GetMemoryMap, 5,
                               &mapSize, NULL, &mapKey, &descSize, &descVersion);

    if (status != EFI_BUFFER_TOO_SMALL) {
        Print(L"GetMemoryMap unexpected status: %r\n", status);
        MemoryMap result = {status, 0, NULL, 0, 0, 0};
        return result;
    }

    mapSize += 4096;

    status = uefi_call_wrapper(BS->AllocatePool, 3,
                               EfiLoaderData, mapSize, (void**)&map);
    if (status != EFI_SUCCESS || map == NULL) {
        Print(L"AllocatePool failed: %r\n", status);
        MemoryMap result = {status, 0, NULL, 0, 0, 0};
        return result;
    }

    status = uefi_call_wrapper(BS->GetMemoryMap, 5,
                               &mapSize, map, &mapKey, &descSize, &descVersion);

    if (status != EFI_SUCCESS) {
        Print(L"GetMemoryMap failed: %r\n", status);
        uefi_call_wrapper(BS->FreePool, 1, map);
        map = NULL;
    }

    MemoryMap result = {status, mapSize, map, mapKey, descSize, descVersion};
    return result;
}