#include <efi.h>
#include <efilib.h>

#include "kernel.h"
#include "main.h"
#include "Initialize/saveMemoryMap.h"
#include "Initialize/baseGraphic.h"
#include "Initialize/baseConsole.h"
#include "Initialize/systemTables.h"

MemoryMap mmp;

BOOLEAN systemIsNotEBS;

EFIAPI
EFI_STATUS
efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    EFI_STATUS status;
    systemIsNotEBS = TRUE;

    InitializeLib(ImageHandle, SystemTable);

    BS->SetWatchdogTimer(0, 0, 0, NULL);
    ST->ConOut->ClearScreen(ST->ConOut);

    Print(L"Getting GOP Graphic\n");
    status = InitGop();
    if (EFI_ERROR(status)) { goto exit; }

    UINT32 BestMode = FindBestMode(1280, 720);
    SetGraphicsMode(BestMode);
    console_init(MakeColor(255, 255, 255), MakeColor(0, 0, 0));

    DrawStringCentered(Graphics.Height / 2 - 10, "MiniUserOS is loading...", MakeColor(255, 255, 255), MakeColor(0, 0, 0));
    
    console_print("Saving Memory Map...\n");
    mmp = SaveMemoryMap();
    console_print("Saving Memory Map OK\n");

    SystemTablesInitialize();

    BS->ExitBootServices(ImageHandle, mmp.mapKey);

    kernel_main();

    goto exit;
    return EFI_SUCCESS;

exit:
    while (1) { __asm__("hlt"); }
}