#include "Tables/idt.h"
#include "Tables/gdt.h"
#include "Root/apic.h"

extern void console_print(const char *str);
extern void console_print_hex(uint64_t val);

/*
 * ============================================================================
 *                              ГЛОБАЛЬНЫЕ ДАННЫЕ
 * ============================================================================
 */

// Таблица прерываний
static idt_entry_t idt[IDT_ENTRIES] __attribute__((aligned(16)));

// Указатель для инструкции LIDT
static idt_pointer_t idt_ptr;

// Таблица высокоуровневых обработчиков
static interrupt_handler_t handlers[IDT_ENTRIES];

/*
 * ============================================================================
 *                      ОБЪЯВЛЕНИЯ ISR (определены ниже в asm)
 * ============================================================================
 */

// Исключения процессора (0-31)
void isr0(void);   void isr1(void);   void isr2(void);   void isr3(void);
void isr4(void);   void isr5(void);   void isr6(void);   void isr7(void);
void isr8(void);   void isr9(void);   void isr10(void);  void isr11(void);
void isr12(void);  void isr13(void);  void isr14(void);  void isr15(void);
void isr16(void);  void isr17(void);  void isr18(void);  void isr19(void);
void isr20(void);  void isr21(void);  void isr22(void);  void isr23(void);
void isr24(void);  void isr25(void);  void isr26(void);  void isr27(void);
void isr28(void);  void isr29(void);  void isr30(void);  void isr31(void);

// IRQ (32-47) — аппаратные прерывания
void irq0(void);   void irq1(void);   void irq2(void);   void irq3(void);
void irq4(void);   void irq5(void);   void irq6(void);   void irq7(void);
void irq8(void);   void irq9(void);   void irq10(void);  void irq11(void);
void irq12(void);  void irq13(void);  void irq14(void);  void irq15(void);

/*
 * ============================================================================
 *                      ISR/IRQ ЗАГЛУШКИ (INLINE ASSEMBLY)
 * ============================================================================
 *
 * Эти заглушки выполняют:
 * 1. Сохранение всех регистров
 * 2. Подготовка стекового фрейма
 * 3. Вызов общего обработчика на C
 * 4. Восстановление регистров
 * 5. Возврат из прерывания
 */

/*
 * Общая заглушка обработчика (top-level assembly)
 */
__asm__(
    ".text\n"
    ".align 16\n"
    
    "isr_common_stub:\n\t"
    
    /* Сохраняем все регистры общего назначения */
    "pushq %rax\n\t"
    "pushq %rbx\n\t"
    "pushq %rcx\n\t"
    "pushq %rdx\n\t"
    "pushq %rsi\n\t"
    "pushq %rdi\n\t"
    "pushq %rbp\n\t"
    "pushq %r8\n\t"
    "pushq %r9\n\t"
    "pushq %r10\n\t"
    "pushq %r11\n\t"
    "pushq %r12\n\t"
    "pushq %r13\n\t"
    "pushq %r14\n\t"
    "pushq %r15\n\t"
    
    /* Сохраняем сегментные регистры (опционально, для полноты) */
    "movw %ds, %ax\n\t"
    "pushq %rax\n\t"
    "movw %es, %ax\n\t"
    "pushq %rax\n\t"
    
    /* Загружаем сегменты данных ядра */
    "movw $0x10, %ax\n\t"      /* GDT_KERNEL_DATA_SELECTOR */
    "movw %ax, %ds\n\t"
    "movw %ax, %es\n\t"
    
    /* Передаём указатель на стековый фрейм как аргумент (System V ABI: rdi) */
    "movq %rsp, %rdi\n\t"
    
    /* Сохраняем rsp в callee-saved регистре */
    "movq %rsp, %rbx\n\t"
    
    /* Выравниваем стек на 16 байт (требование ABI) */
    "andq $-16, %rsp\n\t"
    
    /* Вызываем обработчик на C */
    "call interrupt_common_handler\n\t"
    
    /* Восстанавливаем RSP */
    "movq %rbx, %rsp\n\t"
    
    /* Восстанавливаем сегментные регистры */
    "popq %rax\n\t"
    "movw %ax, %es\n\t"
    "popq %rax\n\t"
    "movw %ax, %ds\n\t"
    
    /* Восстанавливаем регистры в обратном порядке */
    "popq %r15\n\t"
    "popq %r14\n\t"
    "popq %r13\n\t"
    "popq %r12\n\t"
    "popq %r11\n\t"
    "popq %r10\n\t"
    "popq %r9\n\t"
    "popq %r8\n\t"
    "popq %rbp\n\t"
    "popq %rdi\n\t"
    "popq %rsi\n\t"
    "popq %rdx\n\t"
    "popq %rcx\n\t"
    "popq %rbx\n\t"
    "popq %rax\n\t"
    
    /* Убираем номер прерывания и код ошибки */
    "addq $16, %rsp\n\t"
    
    /* Возврат из прерывания */
    "iretq\n\t"
);

/*
 * Макросы для генерации ISR заглушек
 */

/* ISR БЕЗ кода ошибки */
#define ISR_NO_ERROR(num) \
    __asm__( \
        ".text\n" \
        ".globl isr" #num "\n" \
        ".align 16\n" \
        "isr" #num ":\n\t" \
        "pushq $0\n\t"          /* Фиктивный код ошибки */ \
        "pushq $" #num "\n\t"   /* Номер прерывания */ \
        "jmp isr_common_stub\n\t" \
    );

/* ISR С кодом ошибки (процессор уже положил код на стек) */
#define ISR_ERROR(num) \
    __asm__( \
        ".text\n" \
        ".globl isr" #num "\n" \
        ".align 16\n" \
        "isr" #num ":\n\t" \
        /* Код ошибки уже на стеке */ \
        "pushq $" #num "\n\t"   /* Номер прерывания */ \
        "jmp isr_common_stub\n\t" \
    );

/* IRQ заглушка */
#define IRQ(num, vector) \
    __asm__( \
        ".text\n" \
        ".globl irq" #num "\n" \
        ".align 16\n" \
        "irq" #num ":\n\t" \
        "pushq $0\n\t"              /* Нет кода ошибки */ \
        "pushq $" #vector "\n\t"    /* Номер вектора */ \
        "jmp isr_common_stub\n\t" \
    );

/*
 * Генерация всех ISR заглушек (исключения 0-31)
 */
ISR_NO_ERROR(0)      /* Divide Error */
ISR_NO_ERROR(1)      /* Debug */
ISR_NO_ERROR(2)      /* NMI */
ISR_NO_ERROR(3)      /* Breakpoint */
ISR_NO_ERROR(4)      /* Overflow */
ISR_NO_ERROR(5)      /* Bound Range */
ISR_NO_ERROR(6)      /* Invalid Opcode */
ISR_NO_ERROR(7)      /* Device Not Available */
ISR_ERROR(8)         /* Double Fault */
ISR_NO_ERROR(9)      /* Coprocessor Segment Overrun */
ISR_ERROR(10)        /* Invalid TSS */
ISR_ERROR(11)        /* Segment Not Present */
ISR_ERROR(12)        /* Stack Fault */
ISR_ERROR(13)        /* General Protection */
ISR_ERROR(14)        /* Page Fault */
ISR_NO_ERROR(15)     /* Reserved */
ISR_NO_ERROR(16)     /* x87 FPU */
ISR_ERROR(17)        /* Alignment Check */
ISR_NO_ERROR(18)     /* Machine Check */
ISR_NO_ERROR(19)     /* SIMD */
ISR_NO_ERROR(20)
ISR_NO_ERROR(21)
ISR_ERROR(22)        /* Reserved with error (некоторые CPU) */
ISR_NO_ERROR(23)
ISR_NO_ERROR(24)
ISR_NO_ERROR(25)
ISR_NO_ERROR(26)
ISR_NO_ERROR(27)
ISR_NO_ERROR(28)
ISR_NO_ERROR(29)
ISR_ERROR(30)        /* Security Exception */
ISR_NO_ERROR(31)

/*
 * Генерация IRQ заглушек (векторы 32-47)
 */
IRQ(0,  32)          /* Timer */
IRQ(1,  33)          /* Keyboard */
IRQ(2,  34)          /* Cascade */
IRQ(3,  35)          /* COM2 */
IRQ(4,  36)          /* COM1 */
IRQ(5,  37)          /* LPT2 */
IRQ(6,  38)          /* Floppy */
IRQ(7,  39)          /* LPT1/Spurious */
IRQ(8,  40)          /* RTC */
IRQ(9,  41)          /* ACPI */
IRQ(10, 42)          /* Open */
IRQ(11, 43)          /* Open */
IRQ(12, 44)          /* Mouse */
IRQ(13, 45)          /* FPU */
IRQ(14, 46)          /* Primary ATA */
IRQ(15, 47)          /* Secondary ATA */

/*
 * ============================================================================
 *                         ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ
 * ============================================================================
 */

// Короче, тут раньше заглушки были, а теперь реальные функции, я их оставил ради совместимости
static void kprint(const char* str) {
    console_print(str);
}

static void kprint_hex(uint64_t value) {
    console_print_hex(value);
}

/*
 * Создание записи IDT
 */
void idt_set_gate(uint8_t vector, uint64_t handler, uint8_t ist, uint8_t type_attr) {
    idt_entry_t* entry = &idt[vector];
    
    // Разбиваем адрес обработчика на части
    entry->offset_low    = handler & 0xFFFF;
    entry->offset_middle = (handler >> 16) & 0xFFFF;
    entry->offset_high   = (handler >> 32) & 0xFFFFFFFF;
    
    // Селектор сегмента кода ядра
    entry->selector = GDT_KERNEL_CODE_SELECTOR;
    
    // IST индекс (0 = не использовать)
    entry->ist = ist & 0x07;
    
    // Тип и атрибуты
    entry->type_attr = type_attr;
    
    // Зарезервированное поле
    entry->reserved = 0;
}

/*
 * Общий обработчик прерываний (вызывается из ассемблера)
 * 
 * ВАЖНО: Эта функция НЕ должна быть static, чтобы линкер мог найти её!
 */
void interrupt_common_handler(interrupt_frame_t* frame) {
    uint8_t vector = frame->interrupt_number;
    
    // Если есть зарегистрированный обработчик — вызываем
    if (handlers[vector] != 0) {
        handlers[vector](frame);
        return;
    }
    
    // Обработка по умолчанию для исключений
    if (vector < 32) {
        // Названия исключений для отладки
        static const char* exception_names[] = {
            "Divide Error", "Debug", "NMI", "Breakpoint",
            "Overflow", "Bound Range", "Invalid Opcode", "Device Not Available",
            "Double Fault", "Coprocessor", "Invalid TSS", "Segment Not Present",
            "Stack Fault", "General Protection", "Page Fault", "Reserved",
            "x87 FPU", "Alignment Check", "Machine Check", "SIMD"
        };
        
        kprint("\n!!! EXCEPTION: ");
        if (vector < 20) {
            kprint(exception_names[vector]);
        }
        kprint("\n");
        
        kprint("Vector: "); kprint_hex(vector); kprint("\n");
        kprint("Error:  "); kprint_hex(frame->error_code); kprint("\n");
        kprint("RIP:    "); kprint_hex(frame->rip); kprint("\n");
        kprint("RSP:    "); kprint_hex(frame->rsp); kprint("\n");
        kprint("CS:     "); kprint_hex(frame->cs); kprint("\n");
        kprint("RFLAGS: "); kprint_hex(frame->rflags); kprint("\n");
        
        // Для Page Fault выводим CR2 (адрес ошибки)
        if (vector == EXCEPTION_PAGE_FAULT) {
            uint64_t cr2;
            __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
            kprint("CR2:    "); kprint_hex(cr2); kprint("\n");
        }
        
        // Зависаем
        kprint("\nSystem halted.\n");
        while (1) {
            __asm__ volatile("cli; hlt");
        }
    }
    
    // Для аппаратных прерываний без обработчика — просто возвращаемся
    // (но нужно послать EOI, если используется PIC/APIC)
    lapic_send_eoi();
}

/*
 * Загрузка IDT
 */
static void idt_load(void) {
    __asm__ volatile("lidt %0" : : "m"(idt_ptr));
}

/*
 * ============================================================================
 *                         ПУБЛИЧНЫЕ ФУНКЦИИ
 * ============================================================================
 */

void idt_init(void) {
    // Обнуляем таблицу обработчиков
    for (int i = 0; i < IDT_ENTRIES; i++) {
        handlers[i] = 0;
    }
    
    // Обнуляем IDT
    uint8_t* idt_bytes = (uint8_t*)idt;
    for (uint32_t i = 0; i < sizeof(idt); i++) {
        idt_bytes[i] = 0;
    }
    
    /*
     * Устанавливаем обработчики исключений
     * Используем Interrupt Gate (IF очищается автоматически)
     */
    uint8_t attr = IDT_ATTR_PRESENT | IDT_ATTR_RING0 | IDT_TYPE_INTERRUPT;
    
    // Исключения 0-31
    idt_set_gate(0,  (uint64_t)isr0,  IST_NONE, attr);
    idt_set_gate(1,  (uint64_t)isr1,  IST_NONE, attr);
    idt_set_gate(2,  (uint64_t)isr2,  IST_NMI,  attr);  // NMI использует IST
    idt_set_gate(3,  (uint64_t)isr3,  IST_NONE, attr | IDT_ATTR_RING3); // INT3 из userspace
    idt_set_gate(4,  (uint64_t)isr4,  IST_NONE, attr);
    idt_set_gate(5,  (uint64_t)isr5,  IST_NONE, attr);
    idt_set_gate(6,  (uint64_t)isr6,  IST_NONE, attr);
    idt_set_gate(7,  (uint64_t)isr7,  IST_NONE, attr);
    idt_set_gate(8,  (uint64_t)isr8,  IST_DOUBLE_FAULT, attr); // Double Fault — IST!
    idt_set_gate(9,  (uint64_t)isr9,  IST_NONE, attr);
    idt_set_gate(10, (uint64_t)isr10, IST_NONE, attr);
    idt_set_gate(11, (uint64_t)isr11, IST_NONE, attr);
    idt_set_gate(12, (uint64_t)isr12, IST_NONE, attr);
    idt_set_gate(13, (uint64_t)isr13, IST_NONE, attr);
    idt_set_gate(14, (uint64_t)isr14, IST_NONE, attr);
    idt_set_gate(15, (uint64_t)isr15, IST_NONE, attr);
    idt_set_gate(16, (uint64_t)isr16, IST_NONE, attr);
    idt_set_gate(17, (uint64_t)isr17, IST_NONE, attr);
    idt_set_gate(18, (uint64_t)isr18, IST_MACHINE_CHECK, attr); // Machine Check — IST!
    idt_set_gate(19, (uint64_t)isr19, IST_NONE, attr);
    idt_set_gate(20, (uint64_t)isr20, IST_NONE, attr);
    idt_set_gate(21, (uint64_t)isr21, IST_NONE, attr);
    idt_set_gate(22, (uint64_t)isr22, IST_NONE, attr);
    idt_set_gate(23, (uint64_t)isr23, IST_NONE, attr);
    idt_set_gate(24, (uint64_t)isr24, IST_NONE, attr);
    idt_set_gate(25, (uint64_t)isr25, IST_NONE, attr);
    idt_set_gate(26, (uint64_t)isr26, IST_NONE, attr);
    idt_set_gate(27, (uint64_t)isr27, IST_NONE, attr);
    idt_set_gate(28, (uint64_t)isr28, IST_NONE, attr);
    idt_set_gate(29, (uint64_t)isr29, IST_NONE, attr);
    idt_set_gate(30, (uint64_t)isr30, IST_NONE, attr);
    idt_set_gate(31, (uint64_t)isr31, IST_NONE, attr);
    
    // IRQ 0-15 (векторы 32-47)
    idt_set_gate(32, (uint64_t)irq0,  IST_NONE, attr);
    idt_set_gate(33, (uint64_t)irq1,  IST_NONE, attr);
    idt_set_gate(34, (uint64_t)irq2,  IST_NONE, attr);
    idt_set_gate(35, (uint64_t)irq3,  IST_NONE, attr);
    idt_set_gate(36, (uint64_t)irq4,  IST_NONE, attr);
    idt_set_gate(37, (uint64_t)irq5,  IST_NONE, attr);
    idt_set_gate(38, (uint64_t)irq6,  IST_NONE, attr);
    idt_set_gate(39, (uint64_t)irq7,  IST_NONE, attr);
    idt_set_gate(40, (uint64_t)irq8,  IST_NONE, attr);
    idt_set_gate(41, (uint64_t)irq9,  IST_NONE, attr);
    idt_set_gate(42, (uint64_t)irq10, IST_NONE, attr);
    idt_set_gate(43, (uint64_t)irq11, IST_NONE, attr);
    idt_set_gate(44, (uint64_t)irq12, IST_NONE, attr);
    idt_set_gate(45, (uint64_t)irq13, IST_NONE, attr);
    idt_set_gate(46, (uint64_t)irq14, IST_NONE, attr);
    idt_set_gate(47, (uint64_t)irq15, IST_NONE, attr);
    
    // Настраиваем указатель IDT
    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base  = (uint64_t)&idt;
    
    // Загружаем IDT
    idt_load();
}

void idt_register_handler(uint8_t vector, interrupt_handler_t handler) {
    handlers[vector] = handler;
}
