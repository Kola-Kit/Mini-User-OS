#ifndef IDT_H
#define IDT_H

#include <stdint.h>

/*
 * ============================================================================
 *                              СТРУКТУРЫ IDT
 * ============================================================================
 * 
 * IDT содержит до 256 дескрипторов прерываний.
 * Каждый дескриптор указывает на обработчик (ISR - Interrupt Service Routine).
 */

// Дескриптор прерывания для x64 (16 байт)
typedef struct __attribute__((packed)) {
    uint16_t offset_low;     // Биты 0-15 адреса обработчика
    uint16_t selector;       // Селектор сегмента кода (из GDT)
    uint8_t  ist;            // Биты 0-2: IST index, остальное = 0
    uint8_t  type_attr;      // Тип и атрибуты
    uint16_t offset_middle;  // Биты 16-31 адреса
    uint32_t offset_high;    // Биты 32-63 адреса
    uint32_t reserved;       // Должно быть 0
} idt_entry_t;

// Указатель на IDT для инструкции LIDT
typedef struct __attribute__((packed)) {
    uint16_t limit;          // Размер таблицы - 1
    uint64_t base;           // Адрес таблицы
} idt_pointer_t;

/*
 * Стековый фрейм, сохраняемый процессором при прерывании
 * 
 * Процессор автоматически сохраняет эти значения на стек
 * перед вызовом обработчика.
 */
typedef struct __attribute__((packed)) {
    // Сохранённые нами сегментные регистры
    uint64_t es;
    uint64_t ds;
    
    // Сохранённые нами GPR
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    
    // Наши push (номер + код ошибки)
    uint64_t interrupt_number;
    uint64_t error_code;
    
    // Автоматически сохранённые процессором
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} interrupt_frame_t;

/*
 * ============================================================================
 *                              КОНСТАНТЫ
 * ============================================================================
 */

// Количество записей в IDT
#define IDT_ENTRIES 256

// Типы шлюзов (gate types)
#define IDT_TYPE_INTERRUPT  0x0E    // Interrupt Gate (IF очищается)
#define IDT_TYPE_TRAP       0x0F    // Trap Gate (IF не изменяется)

// Атрибуты
#define IDT_ATTR_PRESENT    (1 << 7)   // Дескриптор действителен
#define IDT_ATTR_RING0      (0 << 5)   // DPL = 0 (только ядро)
#define IDT_ATTR_RING3      (3 << 5)   // DPL = 3 (user space может вызвать)

// Номера исключений процессора
#define EXCEPTION_DIVIDE_ERROR          0   // #DE - деление на ноль
#define EXCEPTION_DEBUG                 1   // #DB - отладка
#define EXCEPTION_NMI                   2   // NMI - немаскируемое прерывание
#define EXCEPTION_BREAKPOINT            3   // #BP - точка останова
#define EXCEPTION_OVERFLOW              4   // #OF - переполнение
#define EXCEPTION_BOUND_RANGE           5   // #BR - выход за границы
#define EXCEPTION_INVALID_OPCODE        6   // #UD - недопустимая инструкция
#define EXCEPTION_DEVICE_NOT_AVAILABLE  7   // #NM - FPU недоступен
#define EXCEPTION_DOUBLE_FAULT          8   // #DF - двойная ошибка
#define EXCEPTION_COPROCESSOR           9   // устаревшее
#define EXCEPTION_INVALID_TSS           10  // #TS - невалидный TSS
#define EXCEPTION_SEGMENT_NOT_PRESENT   11  // #NP - сегмент отсутствует
#define EXCEPTION_STACK_FAULT           12  // #SS - ошибка стека
#define EXCEPTION_GENERAL_PROTECTION    13  // #GP - общая защита
#define EXCEPTION_PAGE_FAULT            14  // #PF - страничная ошибка
#define EXCEPTION_RESERVED_15           15  // зарезервировано
#define EXCEPTION_X87_FPU               16  // #MF - ошибка x87 FPU
#define EXCEPTION_ALIGNMENT_CHECK       17  // #AC - ошибка выравнивания
#define EXCEPTION_MACHINE_CHECK         18  // #MC - машинная проверка
#define EXCEPTION_SIMD                  19  // #XM - SIMD исключение

// Диапазоны прерываний
#define IRQ_BASE            32          // Первый IRQ (после исключений)
#define IRQ_COUNT           16          // Количество IRQ (для PIC)
#define SYSCALL_VECTOR      0x80        // Системные вызовы (опционально)

// Индексы IST (для критических исключений)
#define IST_NONE            0           // Использовать текущий стек
#define IST_DOUBLE_FAULT    1           // IST1 для Double Fault
#define IST_NMI             2           // IST2 для NMI
#define IST_MACHINE_CHECK   3           // IST3 для Machine Check

/*
 * ============================================================================
 *                              ФУНКЦИИ
 * ============================================================================
 */

// Инициализация IDT
void idt_init(void);

// Установка обработчика для конкретного вектора
void idt_set_gate(uint8_t vector, uint64_t handler, uint8_t ist, uint8_t type_attr);

// Типы обработчиков
typedef void (*interrupt_handler_t)(interrupt_frame_t* frame);

// Регистрация обработчика (высокоуровневая функция)
void idt_register_handler(uint8_t vector, interrupt_handler_t handler);

// Включение/выключение прерываний
static inline void interrupts_enable(void) {
    __asm__ volatile("sti");
}

static inline void interrupts_disable(void) {
    __asm__ volatile("cli");
}

static inline int interrupts_enabled(void) {
    uint64_t flags;
    __asm__ volatile("pushfq; popq %0" : "=r"(flags));
    return (flags & 0x200) != 0;  // IF flag
}

#endif // IDT_H