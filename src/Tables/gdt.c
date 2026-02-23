#include "Tables/gdt.h"

/*
 * ============================================================================
 *                              ГЛОБАЛЬНЫЕ ДАННЫЕ
 * ============================================================================
 */

// Количество записей в GDT:
// 0: NULL, 1: Kernel Code, 2: Kernel Data, 3: User Code, 4: User Data, 5-6: TSS
#define GDT_ENTRIES 7

// Выравнивание по 16 байт для производительности
static gdt_entry_t gdt[GDT_ENTRIES] __attribute__((aligned(16)));

// TSS — одна структура на процессор (для SMP нужен массив)
static tss_t tss __attribute__((aligned(16)));

// Указатель на GDT для инструкции LGDT
static gdt_pointer_t gdt_ptr;

// Аварийные стеки для IST
// Каждый стек — отдельная область памяти
static uint8_t ist_stack1[IST_STACK_SIZE] __attribute__((aligned(16)));
static uint8_t ist_stack2[IST_STACK_SIZE] __attribute__((aligned(16)));
static uint8_t ist_stack3[IST_STACK_SIZE] __attribute__((aligned(16)));

// Стек ядра для RSP0 (используется при переходе из Ring 3)
static uint8_t kernel_stack[IST_STACK_SIZE * 2] __attribute__((aligned(16)));

/*
 * ============================================================================
 *                         ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ
 * ============================================================================
 */

/*
 * Создание обычного дескриптора сегмента
 * 
 * Параметры:
 *   base   - базовый адрес (игнорируется в x64 для кода/данных)
 *   limit  - размер сегмента (игнорируется в x64)
 *   access - флаги доступа
 *   flags  - флаги гранулярности
 * 
 * Возвращает готовую структуру дескриптора
 */
static gdt_entry_t gdt_create_entry(uint32_t base, uint32_t limit, 
                                     uint8_t access, uint8_t flags) {
    gdt_entry_t entry;
    
    // Разбиваем базовый адрес на части
    entry.base_low    = base & 0xFFFF;
    entry.base_middle = (base >> 16) & 0xFF;
    entry.base_high   = (base >> 24) & 0xFF;
    
    // Разбиваем лимит на части
    entry.limit_low   = limit & 0xFFFF;
    entry.granularity = (limit >> 16) & 0x0F;  // Биты 16-19 лимита
    
    // Добавляем флаги в верхние 4 бита granularity
    entry.granularity |= (flags & 0xF0);
    
    entry.access = access;
    
    return entry;
}

/*
 * Создание дескриптора TSS (16 байт)
 * 
 * TSS дескриптор отличается от обычного:
 * - Занимает 2 слота в GDT
 * - Хранит полный 64-битный адрес
 * - Тип = 0x09 (available TSS)
 */
static void gdt_create_tss_entry(int index, uint64_t tss_address, uint32_t tss_size) {
    // TSS дескриптор занимает два соседних слота
    tss_descriptor_t* tss_desc = (tss_descriptor_t*)&gdt[index];
    
    // Размер TSS минус 1 (как требует спецификация)
    uint32_t limit = tss_size - 1;
    
    // Разбиваем адрес на части
    tss_desc->limit_low    = limit & 0xFFFF;
    tss_desc->base_low     = tss_address & 0xFFFF;
    tss_desc->base_middle1 = (tss_address >> 16) & 0xFF;
    tss_desc->base_middle2 = (tss_address >> 24) & 0xFF;
    tss_desc->base_high    = (tss_address >> 32) & 0xFFFFFFFF;
    
    // Флаги доступа: Present + Ring 0 + System + TSS Available
    tss_desc->access = GDT_ACCESS_PRESENT | TSS_TYPE_AVAILABLE;
    
    // Гранулярность: верхние биты лимита
    tss_desc->granularity = (limit >> 16) & 0x0F;
    
    // Зарезервированные поля должны быть 0
    tss_desc->reserved = 0;
}

/*
 * Инициализация структуры TSS
 */
static void tss_init(void) {
    // Обнуляем всю структуру
    uint8_t* tss_ptr = (uint8_t*)&tss;
    for (uint32_t i = 0; i < sizeof(tss_t); i++) {
        tss_ptr[i] = 0;
    }
    
    // Устанавливаем стек ядра (RSP0)
    // При переходе из Ring 3 в Ring 0 процессор загрузит этот стек
    tss.rsp0 = (uint64_t)kernel_stack + sizeof(kernel_stack);
    
    // Устанавливаем аварийные стеки IST
    // Стеки растут вниз, поэтому указываем на КОНЕЦ массива
    tss.ist1 = (uint64_t)ist_stack1 + IST_STACK_SIZE;  // Double Fault
    tss.ist2 = (uint64_t)ist_stack2 + IST_STACK_SIZE;  // NMI
    tss.ist3 = (uint64_t)ist_stack3 + IST_STACK_SIZE;  // Machine Check
    
    // IOPB отсутствует — указываем за пределы TSS
    tss.iopb_offset = sizeof(tss_t);
}

/*
 * Загрузка GDT и сегментных регистров (на ассемблере)
 * 
 * Это низкоуровневая операция, которую нельзя сделать на чистом C.
 * Мы используем inline assembly.
 */
static void gdt_load(void) {
    // Загружаем GDTR (Global Descriptor Table Register)
    __asm__ volatile("lgdt %0" : : "m"(gdt_ptr));
    
    // Загружаем сегментные регистры данных
    // В x64 мы просто загружаем селекторы, база/лимит игнорируются
    __asm__ volatile(
        "mov %0, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %%ax, %%ss\n"
        : : "i"(GDT_KERNEL_DATA_SELECTOR)
        : "ax"
    );
    
    // Для CS нужен far jump (обычный mov не работает)
    // Используем трюк с retfq: кладём адрес и селектор на стек
    __asm__ volatile(
        "pushq %0\n"           // Селектор CS
        "leaq 1f(%%rip), %%rax\n"  // Адрес метки "1:"
        "pushq %%rax\n"
        "lretq\n"              // Far return = загрузка CS:RIP
        "1:\n"
        : : "i"(GDT_KERNEL_CODE_SELECTOR)
        : "rax", "memory"
    );
}

/*
 * Загрузка TSS в Task Register
 */
static void tss_load(void) {
    __asm__ volatile("ltr %0" : : "r"((uint16_t)GDT_TSS_SELECTOR));
}

/*
 * ============================================================================
 *                         ПУБЛИЧНЫЕ ФУНКЦИИ
 * ============================================================================
 */

void gdt_init(void) {
    /*
     * Шаг 1: Создаём записи GDT
     * 
     * Структура нашей GDT:
     * [0] NULL дескриптор (обязателен, процессор игнорирует его)
     * [1] Код ядра (Ring 0, 64-bit)
     * [2] Данные ядра (Ring 0)
     * [3] Код пользователя (Ring 3, 64-bit)
     * [4] Данные пользователя (Ring 3)
     * [5-6] TSS (занимает 2 слота)
     */
    
    // [0] NULL — первый дескриптор всегда нулевой
    gdt[0] = gdt_create_entry(0, 0, 0, 0);
    
    // [1] Код ядра
    // Access: Present + Ring 0 + Code/Data + Executable + Readable
    // Flags: Long mode (64-bit)
    gdt[1] = gdt_create_entry(
        0, 0xFFFFF,
        GDT_ACCESS_PRESENT | GDT_ACCESS_RING0 | GDT_ACCESS_CODEDATA |
        GDT_ACCESS_EXECUTABLE | GDT_ACCESS_RW,
        GDT_FLAG_LONG_MODE | GDT_FLAG_GRANULARITY
    );
    
    // [2] Данные ядра
    // Access: Present + Ring 0 + Code/Data + Writable
    // Flags: нет Long mode (данные не требуют)
    gdt[2] = gdt_create_entry(
        0, 0xFFFFF,
        GDT_ACCESS_PRESENT | GDT_ACCESS_RING0 | GDT_ACCESS_CODEDATA |
        GDT_ACCESS_RW,
        GDT_FLAG_GRANULARITY
    );
    
    // [3] Код пользователя (для будущего user space)
    gdt[3] = gdt_create_entry(
        0, 0xFFFFF,
        GDT_ACCESS_PRESENT | GDT_ACCESS_RING3 | GDT_ACCESS_CODEDATA |
        GDT_ACCESS_EXECUTABLE | GDT_ACCESS_RW,
        GDT_FLAG_LONG_MODE | GDT_FLAG_GRANULARITY
    );
    
    // [4] Данные пользователя
    gdt[4] = gdt_create_entry(
        0, 0xFFFFF,
        GDT_ACCESS_PRESENT | GDT_ACCESS_RING3 | GDT_ACCESS_CODEDATA |
        GDT_ACCESS_RW,
        GDT_FLAG_GRANULARITY
    );
    
    /*
     * Шаг 2: Инициализируем TSS
     */
    tss_init();
    
    // [5-6] TSS дескриптор (16 байт = 2 слота)
    gdt_create_tss_entry(5, (uint64_t)&tss, sizeof(tss_t));
    
    /*
     * Шаг 3: Настраиваем указатель GDT
     */
    gdt_ptr.limit = sizeof(gdt) - 1;
    gdt_ptr.base  = (uint64_t)&gdt;
    
    /*
     * Шаг 4: Загружаем GDT и TSS
     */
    gdt_load();
    tss_load();
}

void tss_set_kernel_stack(uint64_t stack_top) {
    // Вызывается при переключении задач
    // Устанавливает стек, который будет использоваться
    // при переходе из user space в kernel space
    tss.rsp0 = stack_top;
}

tss_t* tss_get(void) {
    return &tss;
}