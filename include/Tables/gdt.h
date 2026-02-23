#ifndef GDT_H
#define GDT_H

#include <stdint.h>

/*
 * ============================================================================
 *                              СТРУКТУРЫ GDT
 * ============================================================================
 * 
 * Каждый дескриптор GDT занимает 8 байт и описывает сегмент памяти.
 * В x64 для кода/данных сегментация игнорируется (flat model),
 * но дескрипторы всё равно нужны для указания привилегий (Ring 0/3).
 */

// Дескриптор сегмента (8 байт)
// Упаковываем, чтобы компилятор не добавлял padding
typedef struct __attribute__((packed)) {
    uint16_t limit_low;      // Биты 0-15 лимита
    uint16_t base_low;       // Биты 0-15 базового адреса
    uint8_t  base_middle;    // Биты 16-23 базового адреса
    uint8_t  access;         // Флаги доступа (тип сегмента, привилегии)
    uint8_t  granularity;    // Флаги + биты 16-19 лимита
    uint8_t  base_high;      // Биты 24-31 базового адреса
} gdt_entry_t;

/*
 * TSS дескриптор в x64 занимает 16 байт (два слота GDT),
 * потому что должен хранить полный 64-битный адрес TSS.
 */
typedef struct __attribute__((packed)) {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle1;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_middle2;
    uint32_t base_high;      // Верхние 32 бита адреса (для x64)
    uint32_t reserved;       // Зарезервировано, должно быть 0
} tss_descriptor_t;

/*
 * TSS (Task State Segment) для x64
 * 
 * В 64-битном режиме TSS используется только для:
 * 1. RSP0-RSP2: Стеки для переключения привилегий
 * 2. IST1-IST7: Аварийные стеки для критических прерываний
 * 3. IOPB: Карта разрешений I/O портов (опционально)
 */
typedef struct __attribute__((packed)) {
    uint32_t reserved0;      // Исторический мусор, игнорируется
    
    // Указатели стека для каждого уровня привилегий
    // RSP0 используется при переходе из Ring 3 в Ring 0
    uint64_t rsp0;           // Стек для Ring 0 (ядро)
    uint64_t rsp1;           // Стек для Ring 1 (не используется в большинстве ОС)
    uint64_t rsp2;           // Стек для Ring 2 (не используется в большинстве ОС)
    
    uint64_t reserved1;      // Зарезервировано
    
    // IST (Interrupt Stack Table) — аварийные стеки
    // Используются для критических исключений, чтобы гарантировать
    // работоспособный стек даже при stack overflow
    uint64_t ist1;           // Аварийный стек #1 (например, для Double Fault)
    uint64_t ist2;           // Аварийный стек #2 (например, для NMI)
    uint64_t ist3;           // Аварийный стек #3
    uint64_t ist4;           // Аварийный стек #4
    uint64_t ist5;           // Аварийный стек #5
    uint64_t ist6;           // Аварийный стек #6
    uint64_t ist7;           // Аварийный стек #7
    
    uint64_t reserved2;      // Зарезервировано
    uint16_t reserved3;      // Зарезервировано
    
    // Смещение до IOPB (I/O Permission Bitmap)
    // Если равно размеру TSS, IOPB отсутствует
    uint16_t iopb_offset;
} tss_t;

/*
 * Указатель на GDT (загружается инструкцией LGDT)
 * Формат специфичен для x64: 2 байта размер + 8 байт адрес
 */
typedef struct __attribute__((packed)) {
    uint16_t limit;          // Размер таблицы - 1
    uint64_t base;           // Линейный адрес таблицы
} gdt_pointer_t;

/*
 * ============================================================================
 *                              КОНСТАНТЫ
 * ============================================================================
 */

// Индексы сегментов в GDT (в байтах, как селекторы)
#define GDT_NULL_SELECTOR       0x00    // Нулевой сегмент (обязателен)
#define GDT_KERNEL_CODE_SELECTOR 0x08   // Код ядра (Ring 0)
#define GDT_KERNEL_DATA_SELECTOR 0x10   // Данные ядра (Ring 0)
#define GDT_USER_CODE_SELECTOR  0x18    // Код пользователя (Ring 3) | 3
#define GDT_USER_DATA_SELECTOR  0x20    // Данные пользователя (Ring 3) | 3
#define GDT_TSS_SELECTOR        0x28    // TSS (занимает 2 слота)

// RPL (Requested Privilege Level) добавляется к селектору
#define RPL_KERNEL              0x00    // Ring 0
#define RPL_USER                0x03    // Ring 3

// Флаги доступа сегментов (байт access)
#define GDT_ACCESS_PRESENT      (1 << 7)  // Сегмент присутствует в памяти
#define GDT_ACCESS_RING0        (0 << 5)  // Уровень привилегий 0
#define GDT_ACCESS_RING3        (3 << 5)  // Уровень привилегий 3
#define GDT_ACCESS_SYSTEM       (0 << 4)  // Системный сегмент (TSS, LDT)
#define GDT_ACCESS_CODEDATA     (1 << 4)  // Сегмент кода/данных
#define GDT_ACCESS_EXECUTABLE   (1 << 3)  // Исполняемый (код)
#define GDT_ACCESS_DC           (1 << 2)  // Direction/Conforming
#define GDT_ACCESS_RW           (1 << 1)  // Readable (код) / Writable (данные)
#define GDT_ACCESS_ACCESSED     (1 << 0)  // Сегмент был использован

// Флаги гранулярности
#define GDT_FLAG_LONG_MODE      (1 << 5)  // 64-битный сегмент кода
#define GDT_FLAG_SIZE_32        (1 << 6)  // 32-битный сегмент (0 для 64-bit)
#define GDT_FLAG_GRANULARITY    (1 << 7)  // Лимит в страницах (4KB)

// Типы системных сегментов (для TSS)
#define TSS_TYPE_AVAILABLE      0x09      // TSS доступен
#define TSS_TYPE_BUSY           0x0B      // TSS занят

// Размеры аварийных стеков
#define IST_STACK_SIZE          4096      // 4KB на каждый IST стек

/*
 * ============================================================================
 *                              ФУНКЦИИ
 * ============================================================================
 */

// Инициализация GDT и TSS
void gdt_init(void);

// Установка стека ядра (вызывается при переключении задач)
void tss_set_kernel_stack(uint64_t stack_top);

// Получить указатель на TSS (для отладки)
tss_t* tss_get(void);

#endif // GDT_H
