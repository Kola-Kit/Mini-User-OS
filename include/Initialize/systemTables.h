#ifndef SYSTEM_TABLES_H
#define SYSTEM_TABLES_H

#include <stdint.h>
#include <efi.h>

// ============== ACPI структуры ==============

typedef struct {
    char     Signature[8];          // "RSD PTR "
    uint8_t  Checksum;              // сумма первых 20 байт = 0
    char     OEMID[6];              // производитель
    uint8_t  Revision;              // 0 для 1.0, 2 для 2.0+
    uint32_t RsdtAddress;           // 32-битный адрес RSDT (устаревший)
    uint32_t Length;                // длина всей структуры (для ACPI 2.0+)
    uint64_t XsdtAddress;           // 64-битный адрес XSDT (используй это!)
    uint8_t  ExtendedChecksum;      // контрольная сумма всей структуры
    uint8_t  Reserved[3];
} __attribute__((packed)) acpi_rsdp_t;

// Заголовок любой ACPI таблицы (SDT)
typedef struct {
    char     Signature[4];          // например "FACP", "APIC", "HPET"
    uint32_t Length;                // длина всей таблицы
    uint8_t  Revision;
    uint8_t  Checksum;
    char     OEMID[6];
    char     OEMTableID[8];
    uint32_t OEMRevision;
    uint32_t CreatorID;
    uint32_t CreatorRevision;
} __attribute__((packed)) acpi_sdt_header_t;

// XSDT (Extended System Description Table)
typedef struct {
    acpi_sdt_header_t Header;
    uint64_t          Entries[];    // массив указателей на другие таблицы
} __attribute__((packed)) acpi_xsdt_t;

// RSDT (Root System Description Table) - для совместимости
typedef struct {
    acpi_sdt_header_t Header;
    uint32_t          Entries[];    // 32-битные указатели
} __attribute__((packed)) acpi_rsdt_t;

// MADT/APIC таблица (Multiple APIC Description Table)
typedef struct {
    acpi_sdt_header_t Header;
    uint32_t          LocalApicAddress;
    uint32_t          Flags;
    // После этого идут записи переменной длины
} __attribute__((packed)) acpi_madt_t;

// Типы записей MADT
#define MADT_TYPE_LOCAL_APIC          0
#define MADT_TYPE_IO_APIC             1
#define MADT_TYPE_INTERRUPT_OVERRIDE  2
#define MADT_TYPE_NMI_SOURCE          3
#define MADT_TYPE_LOCAL_APIC_NMI      4
#define MADT_TYPE_LOCAL_APIC_OVERRIDE 5
#define MADT_TYPE_IO_SAPIC            6
#define MADT_TYPE_LOCAL_SAPIC         7
#define MADT_TYPE_PLATFORM_IRQ        8
#define MADT_TYPE_LOCAL_X2APIC        9
#define MADT_TYPE_LOCAL_X2APIC_NMI    10

typedef struct {
    uint8_t  Type;
    uint8_t  Length;
} __attribute__((packed)) madt_entry_header_t;

typedef struct {
    madt_entry_header_t Header;
    uint8_t  AcpiProcessorId;
    uint8_t  ApicId;
    uint32_t Flags;                 // бит 0: процессор включён
} __attribute__((packed)) madt_local_apic_t;

typedef struct {
    madt_entry_header_t Header;
    uint8_t  IoApicId;
    uint8_t  Reserved;
    uint32_t IoApicAddress;
    uint32_t GlobalSystemInterruptBase;
} __attribute__((packed)) madt_io_apic_t;

// ============== SMBIOS структуры ==============

typedef struct {
    char     AnchorString[4];       // "_SM_"
    uint8_t  EntryPointChecksum;
    uint8_t  EntryPointLength;
    uint8_t  MajorVersion;
    uint8_t  MinorVersion;
    uint16_t MaxStructureSize;
    uint8_t  EntryPointRevision;
    char     FormattedArea[5];
    char     IntermediateAnchor[5]; // "_DMI_"
    uint8_t  IntermediateChecksum;
    uint16_t StructureTableLength;
    uint32_t StructureTableAddress;
    uint16_t NumberOfStructures;
    uint8_t  BCDRevision;
} __attribute__((packed)) smbios_entry_t;

typedef struct {
    char     AnchorString[5];       // "_SM3_"
    uint8_t  EntryPointChecksum;
    uint8_t  EntryPointLength;
    uint8_t  MajorVersion;
    uint8_t  MinorVersion;
    uint8_t  DocRev;
    uint8_t  EntryPointRevision;
    uint8_t  Reserved;
    uint32_t StructureTableMaxSize;
    uint64_t StructureTableAddress;
} __attribute__((packed)) smbios3_entry_t;

// Заголовок любой SMBIOS структуры
typedef struct {
    uint8_t  Type;
    uint8_t  Length;                // длина форматированной части
    uint16_t Handle;
} __attribute__((packed)) smbios_header_t;

// Типы SMBIOS структур
#define SMBIOS_TYPE_BIOS_INFO        0
#define SMBIOS_TYPE_SYSTEM_INFO      1
#define SMBIOS_TYPE_BASEBOARD_INFO   2
#define SMBIOS_TYPE_CHASSIS_INFO     3
#define SMBIOS_TYPE_PROCESSOR_INFO   4
#define SMBIOS_TYPE_CACHE_INFO       7
#define SMBIOS_TYPE_MEMORY_DEVICE    17
#define SMBIOS_TYPE_END_OF_TABLE     127

// BIOS Information (Type 0)
typedef struct {
    smbios_header_t Header;
    uint8_t  Vendor;                // строка 1
    uint8_t  BiosVersion;           // строка 2
    uint16_t BiosStartSegment;
    uint8_t  BiosReleaseDate;       // строка 3
    uint8_t  BiosRomSize;
    uint64_t BiosCharacteristics;
} __attribute__((packed)) smbios_bios_info_t;

// System Information (Type 1)
typedef struct {
    smbios_header_t Header;
    uint8_t  Manufacturer;          // строка 1
    uint8_t  ProductName;           // строка 2
    uint8_t  Version;               // строка 3
    uint8_t  SerialNumber;          // строка 4
    uint8_t  UUID[16];
    uint8_t  WakeUpType;
    uint8_t  SKUNumber;             // строка 5
    uint8_t  Family;                // строка 6
} __attribute__((packed)) smbios_system_info_t;

// Processor Information (Type 4)
typedef struct {
    smbios_header_t Header;
    uint8_t  SocketDesignation;     // строка
    uint8_t  ProcessorType;
    uint8_t  ProcessorFamily;
    uint8_t  ProcessorManufacturer; // строка
    uint64_t ProcessorID;
    uint8_t  ProcessorVersion;      // строка
    uint8_t  Voltage;
    uint16_t ExternalClock;         // MHz
    uint16_t MaxSpeed;              // MHz
    uint16_t CurrentSpeed;          // MHz
    uint8_t  Status;
    uint8_t  ProcessorUpgrade;
    uint16_t L1CacheHandle;
    uint16_t L2CacheHandle;
    uint16_t L3CacheHandle;
    uint8_t  SerialNumber;          // строка
    uint8_t  AssetTag;              // строка
    uint8_t  PartNumber;            // строка
    uint8_t  CoreCount;
    uint8_t  CoreEnabled;
    uint8_t  ThreadCount;
    uint16_t ProcessorCharacteristics;
} __attribute__((packed)) smbios_processor_info_t;

// Memory Device (Type 17)
typedef struct {
    smbios_header_t Header;
    uint16_t PhysicalMemoryArrayHandle;
    uint16_t MemoryErrorInfoHandle;
    uint16_t TotalWidth;
    uint16_t DataWidth;
    uint16_t Size;                  // в МБ (или КБ если бит 15 = 1)
    uint8_t  FormFactor;
    uint8_t  DeviceSet;
    uint8_t  DeviceLocator;         // строка
    uint8_t  BankLocator;           // строка
    uint8_t  MemoryType;
    uint16_t TypeDetail;
    uint16_t Speed;                 // MHz
    uint8_t  Manufacturer;          // строка
    uint8_t  SerialNumber;          // строка
    uint8_t  AssetTag;              // строка
    uint8_t  PartNumber;            // строка
    uint8_t  Attributes;
    uint32_t ExtendedSize;          // в МБ, если Size = 0x7FFF
    uint16_t ConfiguredSpeed;       // MHz
} __attribute__((packed)) smbios_memory_device_t;

// ============== Глобальные указатели ==============

extern acpi_rsdp_t      *g_RSDP;
extern acpi_xsdt_t      *g_XSDT;
extern acpi_rsdt_t      *g_RSDT;
extern acpi_madt_t      *g_MADT;
extern smbios_entry_t   *g_SMBIOS;
extern smbios3_entry_t  *g_SMBIOS3;

// ============== Функции ==============

// Инициализация - найти все системные таблицы
EFI_STATUS SystemTablesInitialize(void);

// Поиск в конфигурационных таблицах EFI
VOID *FindConfigTable(EFI_GUID *Guid);

// ACPI функции
acpi_rsdp_t *AcpiGetRSDP(void);
acpi_sdt_header_t *AcpiFindTable(const char *signature);
BOOLEAN AcpiValidateChecksum(void *table, UINTN length);

// Подсчёт процессоров из MADT
UINTN AcpiGetProcessorCount(void);

// SMBIOS функции
smbios_header_t *SmbiosFindStructure(uint8_t type);
smbios_header_t *SmbiosGetNext(smbios_header_t *current);
const char *SmbiosGetString(smbios_header_t *header, uint8_t index);

// Вывод информации
void PrintSystemInfo(void);
void PrintACPITables(void);
void PrintSMBIOSInfo(void);

#endif // SYSTEM_TABLES_H