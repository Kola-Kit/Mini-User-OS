#include <efi.h>
#include <efilib.h>
#include <string.h>

#include "kernel.h"
#include "MemoryManager/heap.h"

void UefiVirtualMapSettingUp(void) {
    // 1. Создай КОПИЮ карты памяти
    EFI_MEMORY_DESCRIPTOR *virtual_map = kmalloc(mmp.mapSize);
    memcpy(virtual_map, mmp.map, mmp.mapSize);

    // 2. Заполни VirtualStart для Runtime регионов
    EFI_MEMORY_DESCRIPTOR *desc = virtual_map;
    for (UINTN i = 0; i < mmp.mapSize / mmp.descSize; i++) {
        if (desc->Type == EfiRuntimeServicesCode ||
            desc->Type == EfiRuntimeServicesData ||
            desc->Type == EfiACPIReclaimMemory) {
            
            // Устанавливаем виртуальный адрес (1:1 или KERNEL_VMA + phys)
            desc->VirtualStart = desc->PhysicalStart;  // если отображено 1:1
            // или desc->VirtualStart = KERNEL_VMA + desc->PhysicalStart;
        }
        desc = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)desc + mmp.descSize);
    }

    // 3. Вызови SetVirtualAddressMap с подготовленной картой
    ST->RuntimeServices->SetVirtualAddressMap(
        mmp.mapSize, 
        mmp.descSize, 
        mmp.descVersion, 
        virtual_map
    );

    // 4. Освободи копию (она больше не нужна)
    kfree(virtual_map);
}
