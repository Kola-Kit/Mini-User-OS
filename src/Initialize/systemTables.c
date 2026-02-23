#include <efi.h>
#include <efilib.h>

#include "Initialize/systemTables.h"

extern void console_print(const char* text);
extern void console_print_int(int64_t val);

// ============== GUID определения ==============

// ACPI 2.0+ RSDP
static EFI_GUID gEfiAcpi20TableGuid = {
    0x8868e871, 0xe4f1, 0x11d3,
    {0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81}
};

// ACPI 1.0 RSDP (fallback)
static EFI_GUID gEfiAcpi10TableGuid = {
    0xeb9d2d30, 0x2d88, 0x11d3,
    {0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d}
};

// SMBIOS 3.0+
static EFI_GUID gEfiSmbios3TableGuid = {
    0xf2fd1544, 0x9794, 0x4a2c,
    {0x99, 0x2e, 0xe5, 0xbb, 0xcf, 0x20, 0xe3, 0x94}
};

// SMBIOS 2.x
static EFI_GUID gEfiSmbiosTableGuid = {
    0xeb9d2d31, 0x2d88, 0x11d3,
    {0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d}
};

// ============== Глобальные переменные ==============

acpi_rsdp_t      *g_RSDP    = NULL;
acpi_xsdt_t      *g_XSDT    = NULL;
acpi_rsdt_t      *g_RSDT    = NULL;
acpi_madt_t      *g_MADT    = NULL;
smbios_entry_t   *g_SMBIOS  = NULL;
smbios3_entry_t  *g_SMBIOS3 = NULL;

// ============== Вспомогательные функции ==============

// Сравнение сигнатур (4 байта)
static BOOLEAN CompareSignature(const char *sig1, const char *sig2, UINTN len)
{
    for (UINTN i = 0; i < len; i++) {
        if (sig1[i] != sig2[i]) {
            return FALSE;
        }
    }
    return TRUE;
}

// ============== Основные функции ==============

VOID *FindConfigTable(EFI_GUID *Guid)
{
    for (UINTN i = 0; i < ST->NumberOfTableEntries; i++) {
        EFI_CONFIGURATION_TABLE *Table = &ST->ConfigurationTable[i];
        
        if (CompareGuid(&Table->VendorGuid, Guid) == 0) {
            return Table->VendorTable;
        }
    }
    
    return NULL;
}

// Проверка контрольной суммы
BOOLEAN AcpiValidateChecksum(void *table, UINTN length)
{
    uint8_t *bytes = (uint8_t *)table;
    uint8_t sum = 0;
    
    for (UINTN i = 0; i < length; i++) {
        sum += bytes[i];
    }
    
    return (sum == 0);
}

// Получить RSDP
acpi_rsdp_t *AcpiGetRSDP(void)
{
    acpi_rsdp_t *rsdp = NULL;
    
    // Сначала пробуем ACPI 2.0+
    rsdp = (acpi_rsdp_t *)FindConfigTable(&gEfiAcpi20TableGuid);
    
    if (rsdp == NULL) {
        // Fallback на ACPI 1.0
        rsdp = (acpi_rsdp_t *)FindConfigTable(&gEfiAcpi10TableGuid);
    }
    
    if (rsdp != NULL) {
        // Проверяем сигнатуру "RSD PTR "
        if (!CompareSignature(rsdp->Signature, "RSD PTR ", 8)) {
            console_print("ACPI: Invalid RSDP signature!\n");
            return NULL;
        }
        
        // Проверяем контрольную сумму первых 20 байт
        if (!AcpiValidateChecksum(rsdp, 20)) {
            console_print("ACPI: RSDP checksum failed!\n");
            return NULL;
        }
        
        // Для ACPI 2.0+ проверяем расширенную контрольную сумму
        if (rsdp->Revision >= 2) {
            if (!AcpiValidateChecksum(rsdp, rsdp->Length)) {
                console_print("ACPI: RSDP extended checksum failed!\n");
                return NULL;
            }
        }
    }
    
    return rsdp;
}

// Найти ACPI таблицу по сигнатуре (4 символа)
acpi_sdt_header_t *AcpiFindTable(const char *signature)
{
    if (g_XSDT != NULL) {
        // Используем 64-битную XSDT
        UINTN entries = (g_XSDT->Header.Length - sizeof(acpi_sdt_header_t)) / sizeof(uint64_t);
        
        for (UINTN i = 0; i < entries; i++) {
            acpi_sdt_header_t *header = (acpi_sdt_header_t *)(UINTN)g_XSDT->Entries[i];
            
            if (header != NULL && CompareSignature(header->Signature, signature, 4)) {
                // Проверяем контрольную сумму
                if (AcpiValidateChecksum(header, header->Length)) {
                    return header;
                } else {
                    console_print("ACPI: Table ");
                    console_print(signature);
                    console_print(" checksum failed!\n");
                }
            }
        }
    }
    else if (g_RSDT != NULL) {
        // Fallback на 32-битную RSDT
        UINTN entries = (g_RSDT->Header.Length - sizeof(acpi_sdt_header_t)) / sizeof(uint32_t);
        
        for (UINTN i = 0; i < entries; i++) {
            acpi_sdt_header_t *header = (acpi_sdt_header_t *)(UINTN)g_RSDT->Entries[i];
            
            if (header != NULL && CompareSignature(header->Signature, signature, 4)) {
                if (AcpiValidateChecksum(header, header->Length)) {
                    return header;
                }
            }
        }
    }
    
    return NULL;
}

// Подсчёт процессоров из MADT
UINTN AcpiGetProcessorCount(void)
{
    if (g_MADT == NULL) {
        return 1;  // Минимум 1 процессор
    }
    
    UINTN count = 0;
    uint8_t *ptr = (uint8_t *)g_MADT + sizeof(acpi_madt_t);
    uint8_t *end = (uint8_t *)g_MADT + g_MADT->Header.Length;
    
    while (ptr < end) {
        madt_entry_header_t *entry = (madt_entry_header_t *)ptr;
        
        if (entry->Length == 0) {
            break;  // Защита от бесконечного цикла
        }
        
        if (entry->Type == MADT_TYPE_LOCAL_APIC) {
            madt_local_apic_t *lapic = (madt_local_apic_t *)entry;
            // Проверяем флаг "процессор включён" (бит 0)
            if (lapic->Flags & 0x01) {
                count++;
            }
        }
        else if (entry->Type == MADT_TYPE_LOCAL_X2APIC) {
            // x2APIC для систем с большим числом процессоров
            uint32_t *flags = (uint32_t *)(ptr + 8);
            if (*flags & 0x01) {
                count++;
            }
        }
        
        ptr += entry->Length;
    }
    
    return count > 0 ? count : 1;
}

// ============== SMBIOS функции ==============

// Получить следующую SMBIOS структуру
smbios_header_t *SmbiosGetNext(smbios_header_t *current)
{
    if (current == NULL) {
        return NULL;
    }
    
    // Пропускаем форматированную часть
    uint8_t *ptr = (uint8_t *)current + current->Length;
    
    // Пропускаем строки (заканчиваются двойным нулём)
    while (ptr[0] != 0 || ptr[1] != 0) {
        ptr++;
    }
    ptr += 2;  // Пропускаем двойной ноль
    
    return (smbios_header_t *)ptr;
}

// Найти SMBIOS структуру по типу
smbios_header_t *SmbiosFindStructure(uint8_t type)
{
    uint8_t *tableStart = NULL;
    uint8_t *tableEnd = NULL;
    
    if (g_SMBIOS3 != NULL) {
        tableStart = (uint8_t *)(UINTN)g_SMBIOS3->StructureTableAddress;
        tableEnd = tableStart + g_SMBIOS3->StructureTableMaxSize;
    }
    else if (g_SMBIOS != NULL) {
        tableStart = (uint8_t *)(UINTN)g_SMBIOS->StructureTableAddress;
        tableEnd = tableStart + g_SMBIOS->StructureTableLength;
    }
    else {
        return NULL;
    }
    
    smbios_header_t *header = (smbios_header_t *)tableStart;
    
    while ((uint8_t *)header < tableEnd) {
        if (header->Type == type) {
            return header;
        }
        
        if (header->Type == SMBIOS_TYPE_END_OF_TABLE) {
            break;
        }
        
        header = SmbiosGetNext(header);
        if (header == NULL) {
            break;
        }
    }
    
    return NULL;
}

// Получить строку из SMBIOS структуры
const char *SmbiosGetString(smbios_header_t *header, uint8_t index)
{
    if (header == NULL || index == 0) {
        return "";
    }
    
    // Строки начинаются сразу после форматированной части
    const char *str = (const char *)header + header->Length;
    
    // Перебираем строки
    for (uint8_t i = 1; i < index; i++) {
        // Переходим к следующей строке
        while (*str != '\0') {
            str++;
        }
        str++;  // Пропускаем завершающий ноль
        
        // Если дошли до конца (двойной ноль)
        if (*str == '\0') {
            return "";
        }
    }
    
    return str;
}

// ============== Инициализация ==============

EFI_STATUS SystemTablesInitialize(void)
{
    console_print("Initializing system tables...\n\n");
    
    // === ACPI ===
    g_RSDP = AcpiGetRSDP();
    
    if (g_RSDP != NULL) {
        console_print("ACPI RSDP found at 0x");
        console_print_int((uint64_t)(UINTN)g_RSDP);
        console_print("\n");

        console_print("  Revision: ");
        console_print_int((uint64_t)g_RSDP->Revision == 0 ? 1 : g_RSDP->Revision);
        console_print(".0\n");

        console_print("  OEM ID: ");
        console_print_int((uint64_t)g_RSDP->OEMID);
        console_print("\n");
        
        // Загружаем XSDT или RSDT
        if (g_RSDP->Revision >= 2 && g_RSDP->XsdtAddress != 0) {
            g_XSDT = (acpi_xsdt_t *)(UINTN)g_RSDP->XsdtAddress;
            console_print("  XSDT at 0x");
            console_print_int((uint64_t)g_RSDP->XsdtAddress);
            console_print("\n");
        }
        
        if (g_RSDP->RsdtAddress != 0) {
            g_RSDT = (acpi_rsdt_t *)(UINTN)g_RSDP->RsdtAddress;
            console_print("  RSDT at 0x");
            console_print_int((uint64_t)g_RSDP->RsdtAddress);
            console_print("\n");
        }
        
        // Ищем MADT для информации о процессорах
        g_MADT = (acpi_madt_t *)AcpiFindTable("APIC");
        if (g_MADT != NULL) {
            console_print("  MADT found, Local APIC: 0x");
            console_print_int((uint64_t)g_MADT->LocalApicAddress);
            console_print("\n");

            console_print("  CPU count: ");
            console_print_int((uint64_t)AcpiGetProcessorCount());
            console_print("\n");
        }
    }
    else {
        console_print("ACPI RSDP not found!\n");
    }
    
    console_print("\n");
    
    // === SMBIOS ===
    // Сначала пробуем SMBIOS 3.0
    g_SMBIOS3 = (smbios3_entry_t *)FindConfigTable(&gEfiSmbios3TableGuid);
    
    if (g_SMBIOS3 != NULL) {
        console_print("SMBIOS 3.0 Entry found at 0x");
        console_print_int((uint64_t)(UINTN)g_SMBIOS3);
        console_print("\n");

        console_print("  Version: ");
        console_print_int((uint64_t)g_SMBIOS3->MajorVersion);
        console_print(".");
        console_print_int((uint64_t)g_SMBIOS3->MinorVersion);
        console_print("\n");

        console_print("  Table at: 0x");
        console_print_int((uint64_t)g_SMBIOS3->StructureTableAddress);
        console_print("\n");
    }
    else {
        // Пробуем SMBIOS 2.x
        g_SMBIOS = (smbios_entry_t *)FindConfigTable(&gEfiSmbiosTableGuid);
        
        if (g_SMBIOS != NULL) {
            console_print("SMBIOS 2.x Entry found at 0x");
            console_print_int((uint64_t)(UINTN)g_SMBIOS);
            console_print("\n");

            console_print("  Version: ");
            console_print_int((uint64_t)g_SMBIOS->MajorVersion);
            console_print(".");
            console_print_int((uint64_t)g_SMBIOS->MinorVersion);
            console_print("\n");

            console_print("  Table at: 0x");
            console_print_int((uint64_t)g_SMBIOS->StructureTableAddress);
            console_print("\n");

            console_print("  Structures: ");
            console_print_int((uint64_t)g_SMBIOS->NumberOfStructures);
            console_print("\n");
        }
        else {
            console_print("SMBIOS not found!\n");
        }
    }
    
    console_print("\n");
    
    return EFI_SUCCESS;
}

// ============== Вывод информации ==============

void console_printACPITables(void)
{
    console_print("=== ACPI Tables ===\n");
    
    if (g_XSDT == NULL && g_RSDT == NULL) {
        console_print("No ACPI tables available.\n\n");
        return;
    }
    
    // Список известных таблиц для поиска
    static const char *tables[] = {
        "FACP",  // Fixed ACPI Description Table
        "APIC",  // Multiple APIC Description Table (MADT)
        "HPET",  // High Precision Event Timer
        "MCFG",  // PCI Express Memory Mapped Configuration
        "SSDT",  // Secondary System Description Table
        "BGRT",  // Boot Graphics Resource Table
        "SRAT",  // System Resource Affinity Table
        "SLIT",  // System Locality Distance Information
        "DMAR",  // DMA Remapping Table (Intel VT-d)
        "IVRS",  // I/O Virtualization Reporting Structure (AMD-Vi)
        "FPDT",  // Firmware Performance Data Table
        "WAET",  // Windows ACPI Emulated Devices Table
        NULL
    };
    
    for (int i = 0; tables[i] != NULL; i++) {
        acpi_sdt_header_t *table = AcpiFindTable(tables[i]);
        if (table != NULL) {
            console_print("  ");
            console_print_int((uint64_t)table->Signature);
            console_print(": 0x");
            console_print_int((uint64_t)(UINTN)table);
            console_print(" (size: ");
            console_print_int((uint64_t)table->Length);
            console_print(", rev: ");
            console_print_int((uint64_t)table->Revision);
            console_print(")\n");
        }
    }
    
    console_print("\n");
}

void console_printSMBIOSInfo(void)
{
    console_print("=== SMBIOS Information ===\n");
    
    // BIOS информация
    smbios_bios_info_t *bios = (smbios_bios_info_t *)SmbiosFindStructure(SMBIOS_TYPE_BIOS_INFO);
    if (bios != NULL) {
        console_print("BIOS:\n");

        console_print("  Vendor:  ");
        console_print(SmbiosGetString(&bios->Header, bios->Vendor));
        console_print("\n");

        console_print("  Version: ");
        console_print(SmbiosGetString(&bios->Header, bios->BiosVersion));
        console_print("\n");

        console_print("  Date:    ");
        console_print(SmbiosGetString(&bios->Header, bios->BiosReleaseDate));
        console_print("\n");
    }
    
    // Информация о системе
    smbios_system_info_t *sys = (smbios_system_info_t *)SmbiosFindStructure(SMBIOS_TYPE_SYSTEM_INFO);
    if (sys != NULL) {
        console_print("System:\n");

        console_print("  Manufacturer: ");
        console_print(SmbiosGetString(&sys->Header, sys->Manufacturer));
        console_print("\n");

        console_print("  Product:      ");
        console_print(SmbiosGetString(&sys->Header, sys->ProductName));
        console_print("\n");

        console_print("  Version:      ");
        console_print(SmbiosGetString(&sys->Header, sys->Version));
        console_print("\n");

        console_print("  Serial:       ");
        console_print(SmbiosGetString(&sys->Header, sys->SerialNumber));
        console_print("\n");
    }
    
    // Информация о процессоре
    smbios_processor_info_t *cpu = (smbios_processor_info_t *)SmbiosFindStructure(SMBIOS_TYPE_PROCESSOR_INFO);
    if (cpu != NULL) {
        console_print("Processor:\n");

        console_print("  Socket:       ");
        console_print(SmbiosGetString(&cpu->Header, cpu->SocketDesignation));
        console_print("\n");

        console_print("  Manufacturer: ");
        console_print(SmbiosGetString(&cpu->Header, cpu->ProcessorManufacturer));
        console_print("\n");

        console_print("  Version:      ");
        console_print(SmbiosGetString(&cpu->Header, cpu->ProcessorVersion));
        console_print("\n");

        console_print("  Max Speed:    ");
        console_print_int((uint64_t)cpu->MaxSpeed);
        console_print(" MHz");
        console_print("\n");

        console_print("  Current:      ");
        console_print_int((uint64_t)cpu->CurrentSpeed);
        console_print(" MHz");
        console_print("\n");

        if (cpu->Header.Length >= 0x28) {  // Проверяем, есть ли поля ядер
            console_print("  Cores:        ");
            console_print_int((uint64_t)cpu->CoreCount);
            console_print("\n");

            console_print("  Threads:      %d\n");
            console_print_int((uint64_t)cpu->ThreadCount);
            console_print("\n");
        }
    }
    
    // Информация о памяти
    console_print("Memory Devices:\n");
    smbios_header_t *header = SmbiosFindStructure(SMBIOS_TYPE_MEMORY_DEVICE);
    while (header != NULL && header->Type == SMBIOS_TYPE_MEMORY_DEVICE) {
        smbios_memory_device_t *mem = (smbios_memory_device_t *)header;
        
        // Показываем только установленные модули
        if (mem->Size != 0 && mem->Size != 0xFFFF) {
            UINTN sizeMB;
            if (mem->Size == 0x7FFF) {
                sizeMB = mem->ExtendedSize;
            } else if (mem->Size & 0x8000) {
                sizeMB = (mem->Size & 0x7FFF) / 1024;  // В КБ
            } else {
                sizeMB = mem->Size;  // В МБ
            }
            
            console_print("  ");
            console_print(SmbiosGetString(&mem->Header, mem->DeviceLocator));
            console_print(": ");
            console_print_int((uint64_t)sizeMB);
            console_print(" MB @ ");
            console_print_int((uint64_t)mem->Speed);
            console_print(" MHz (");
            console_print(SmbiosGetString(&mem->Header, mem->DeviceLocator));
            console_print(")\n");
        }
        
        header = SmbiosGetNext(header);
        if (header == NULL || header->Type == SMBIOS_TYPE_END_OF_TABLE) {
            break;
        }
        // Продолжаем искать следующий memory device
        while (header != NULL && header->Type != SMBIOS_TYPE_MEMORY_DEVICE) {
            if (header->Type == SMBIOS_TYPE_END_OF_TABLE) {
                header = NULL;
                break;
            }
            header = SmbiosGetNext(header);
        }
    }
    
    console_print("\n");
}

void console_printSystemInfo(void)
{
    console_print("========================================\n");
    console_print("        SYSTEM INFORMATION\n");
    console_print("========================================\n\n");
    
    console_printACPITables();
    console_printSMBIOSInfo();
}
