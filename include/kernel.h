#ifndef KERNEL_H
#define KERNEL_H

#include <efi.h>
#include <efilib.h>

#include "Initialize/saveMemoryMap.h"
#include "Initialize/baseGraphic.h"

extern MemoryMap mmp;

extern BOOLEAN systemIsNotEBS;

EFIAPI EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable);

#endif