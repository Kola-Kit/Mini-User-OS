#include "Root/apic.h"
#include "Root/io.h"

/* Внешние функции консоли */
extern void console_print(const char* str);
extern void console_print_hex(uint64_t val);

/*
 * ============================================================================
 *                         ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ
 * ============================================================================
 */

static volatile uint32_t* lapic_base = 0;
static volatile uint32_t* ioapic_base = (volatile uint32_t*)IOAPIC_BASE;

/*
 * ============================================================================
 *                         ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ
 * ============================================================================
 */

/* Проверка наличия APIC через CPUID */
static int cpu_has_apic(void) {
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile(
        "cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(1)
    );
    return (edx >> 9) & 1;
}

/* Получение базового адреса Local APIC из MSR */
static uint64_t get_lapic_base(void) {
    uint32_t lo, hi;
    __asm__ volatile(
        "rdmsr"
        : "=a"(lo), "=d"(hi)
        : "c"(0x1B)  /* IA32_APIC_BASE MSR */
    );
    return (((uint64_t)hi << 32) | lo) & 0xFFFFF000;
}

/* Включение Local APIC через MSR */
static void enable_lapic_msr(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0x1B));
    
    lo |= (1 << 11);  /* Global Enable bit */
    
    __asm__ volatile("wrmsr" : : "c"(0x1B), "a"(lo), "d"(hi));
}

/*
 * ============================================================================
 *                         LOCAL APIC
 * ============================================================================
 */

void lapic_write(uint32_t reg, uint32_t value) {
    lapic_base[reg / 4] = value;
    /* Чтение для гарантии завершения записи */
    (void)lapic_base[LAPIC_ID / 4];
}

uint32_t lapic_read(uint32_t reg) {
    return lapic_base[reg / 4];
}

void lapic_send_eoi(void) {
    lapic_write(LAPIC_EOI, 0);
}

uint8_t lapic_get_id(void) {
    return (lapic_read(LAPIC_ID) >> 24) & 0xFF;
}

static void lapic_init(void) {
    /* Включаем APIC через MSR */
    enable_lapic_msr();
    
    /* Получаем базовый адрес */
    lapic_base = (volatile uint32_t*)get_lapic_base();
    
    console_print("Local APIC base: 0x");
    console_print_hex((uint64_t)lapic_base);
    console_print("\n");
    
    /* Устанавливаем Spurious Interrupt Vector и включаем APIC */
    /* Вектор 0xFF для spurious прерываний */
    lapic_write(LAPIC_SVR, LAPIC_SVR_ENABLE | 0xFF);
    
    /* Маскируем все локальные прерывания */
    // lapic_write(LAPIC_TIMER, LAPIC_LVT_MASKED);
    lapic_write(LAPIC_THERMAL, LAPIC_LVT_MASKED);
    lapic_write(LAPIC_PERF, LAPIC_LVT_MASKED);
    lapic_write(LAPIC_LINT0, LAPIC_LVT_MASKED);
    lapic_write(LAPIC_LINT1, LAPIC_LVT_MASKED);
    lapic_write(LAPIC_ERROR, LAPIC_LVT_MASKED);
    
    /* Очищаем регистр ошибок */
    lapic_write(LAPIC_ESR, 0);
    lapic_write(LAPIC_ESR, 0);
    
    /* Сбрасываем Task Priority - принимаем все прерывания */
    lapic_write(LAPIC_TPR, 0);
    
    /* Отправляем EOI на случай pending прерываний */
    lapic_send_eoi();
    
    console_print("Local APIC ID: ");
    console_print_hex(lapic_get_id());
    console_print("\n");
    console_print("Local APIC enabled\n");
}

/*
 * ============================================================================
 *                         I/O APIC
 * ============================================================================
 */

void ioapic_write(uint8_t reg, uint32_t value) {
    ioapic_base[IOAPIC_REGSEL / 4] = reg;
    ioapic_base[IOAPIC_IOWIN / 4] = value;
}

uint32_t ioapic_read(uint8_t reg) {
    ioapic_base[IOAPIC_REGSEL / 4] = reg;
    return ioapic_base[IOAPIC_IOWIN / 4];
}

void ioapic_set_irq(uint8_t irq, uint8_t vector, uint8_t dest_cpu, uint32_t flags) {
    uint32_t reg_lo = IOAPIC_REDTBL + irq * 2;
    uint32_t reg_hi = IOAPIC_REDTBL + irq * 2 + 1;
    
    /* High 32 bits: destination CPU в битах 24-31 */
    ioapic_write(reg_hi, (uint32_t)dest_cpu << 24);
    
    /* Low 32 bits: vector + flags */
    uint32_t entry_lo = vector | flags;
    ioapic_write(reg_lo, entry_lo);
}

void ioapic_mask_irq(uint8_t irq) {
    uint32_t reg = IOAPIC_REDTBL + irq * 2;
    uint32_t value = ioapic_read(reg);
    ioapic_write(reg, value | IOAPIC_INT_MASKED);
}

void ioapic_unmask_irq(uint8_t irq) {
    uint32_t reg = IOAPIC_REDTBL + irq * 2;
    uint32_t value = ioapic_read(reg);
    ioapic_write(reg, value & ~IOAPIC_INT_MASKED);
}

static void ioapic_init(void) {
    console_print("I/O APIC base: 0x");
    console_print_hex((uint64_t)ioapic_base);
    console_print("\n");
    
    uint32_t ver = ioapic_read(IOAPIC_VER);
    uint8_t max_redir = ((ver >> 16) & 0xFF) + 1;
    
    console_print("Max redirections: ");
    console_print_hex(max_redir);
    console_print("\n");
    
    /* Маскируем все прерывания */
    for (uint8_t i = 0; i < max_redir; i++) {
        uint32_t reg_lo = IOAPIC_REDTBL + i * 2;
        ioapic_write(reg_lo, IOAPIC_INT_MASKED | (0x20 + i));
    }
    
    uint8_t cpu_id = lapic_get_id();
    console_print("Routing to CPU: ");
    console_print_hex(cpu_id);
    console_print("\n");
    
    /* 
     * Настройка IRQ1 (клавиатура)
     * 
     * ISA прерывания - edge triggered, active high
     */
    uint32_t keyboard_entry = 
        (IRQ_BASE_VECTOR + IRQ_KEYBOARD) |  /* Vector 33 */
        IOAPIC_INT_EDGE |                    /* Edge triggered */
        IOAPIC_INT_HIGH |                    /* Active high */
        IOAPIC_INT_PHYSICAL |                /* Physical destination */
        IOAPIC_INT_FIXED;                    /* Fixed delivery mode */
    
    /* Настройка IRQ0 (таймер) */
    uint32_t timer_entry = 
        (IRQ_BASE_VECTOR + IRQ_TIMER) |      /* Vector 32 */
        IOAPIC_INT_EDGE |                     /* Edge triggered */
        IOAPIC_INT_HIGH |                      /* Active high */
        IOAPIC_INT_PHYSICAL |                  /* Physical destination */
        IOAPIC_INT_FIXED;                      /* Fixed delivery mode */
    
    /* НЕ добавляем MASKED флаг */
    
    ioapic_write(IOAPIC_REDTBL + IRQ_KEYBOARD * 2 + 1, (uint32_t)cpu_id << 24);
    ioapic_write(IOAPIC_REDTBL + IRQ_KEYBOARD * 2, keyboard_entry);

    ioapic_write(IOAPIC_REDTBL + IRQ_TIMER * 2 + 1, (uint32_t)cpu_id << 24);
    ioapic_write(IOAPIC_REDTBL + IRQ_TIMER * 2, timer_entry);
    
    console_print("Keyboard entry: 0x");
    console_print_hex(keyboard_entry);
    console_print("\n");
    
    /* Проверяем что записалось */
    uint32_t readback = 0;

    readback = ioapic_read(IOAPIC_REDTBL + IRQ_KEYBOARD * 2);
    console_print("Readback: 0x");
    console_print_hex(readback);
    console_print("\n");
    
    if (readback & IOAPIC_INT_MASKED) {
        console_print("WARNING: IRQ1 still masked!\n");
    } else {
        console_print("IRQ1 unmasked OK\n");
    }

    readback = ioapic_read(IOAPIC_REDTBL + IRQ_TIMER * 2);
    console_print("Readback: 0x");
    console_print_hex(readback);
    console_print("\n");
    
    if (readback & IOAPIC_INT_MASKED) {
        console_print("WARNING: IRQ1 still masked!\n");
    } else {
        console_print("IRQ0 unmasked OK\n");
    }
    
    console_print("I/O APIC initialized\n");
}

/*
 * ============================================================================
 *                         ОТКЛЮЧЕНИЕ PIC
 * ============================================================================
 */

static void pic_disable(void) {
    console_print("Disabling legacy PIC...\n");
    
    /* Переинициализируем PIC чтобы перенаправить IRQ подальше */
    /* ICW1 */
    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    io_wait();
    
    /* ICW2: сдвигаем векторы на 0xF0-0xFF чтобы не мешали */
    outb(0x21, 0xF0);
    outb(0xA1, 0xF8);
    io_wait();
    
    /* ICW3 */
    outb(0x21, 0x04);
    outb(0xA1, 0x02);
    io_wait();
    
    /* ICW4 */
    outb(0x21, 0x01);
    outb(0xA1, 0x01);
    io_wait();
    
    /* Маскируем все прерывания PIC */
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);
    
    console_print("PIC disabled\n");
}

/*
 * ============================================================================
 *                         ИНИЦИАЛИЗАЦИЯ PS/2 КЛАВИАТУРЫ
 * ============================================================================
 */

static void keyboard_init(void) {
    console_print("Initializing PS/2 keyboard...\n");
    
    /* Очищаем буфер */
    while (inb(0x64) & 0x01) {
        inb(0x60);
    }
    
    /* Ждём готовности контроллера */
    while (inb(0x64) & 0x02);
    
    /* Включаем первый PS/2 порт */
    outb(0x64, 0xAE);
    io_wait();
    
    /* Читаем конфигурацию */
    while (inb(0x64) & 0x02);
    outb(0x64, 0x20);
    while (!(inb(0x64) & 0x01));
    uint8_t config = inb(0x60);
    
    console_print("PS/2 config before: 0x");
    console_print_hex(config);
    console_print("\n");
    
    /* Включаем IRQ1 (бит 0) и убираем отключение порта 1 (бит 4) */
    config |= 0x01;
    config &= ~0x10;
    
    /* Записываем конфигурацию */
    while (inb(0x64) & 0x02);
    outb(0x64, 0x60);
    while (inb(0x64) & 0x02);
    outb(0x60, config);
    
    console_print("PS/2 config after: 0x");
    console_print_hex(config);
    console_print("\n");
    
    /* Включаем сканирование */
    while (inb(0x64) & 0x02);
    outb(0x60, 0xF4);
    
    /* Ждём ACK (0xFA) */
    int timeout = 100000;
    while (timeout-- > 0) {
        if (inb(0x64) & 0x01) {
            uint8_t ack = inb(0x60);
            console_print("Keyboard response: 0x");
            console_print_hex(ack);
            console_print("\n");
            break;
        }
    }
    
    console_print("Keyboard initialized\n");
}

/*
 * ============================================================================
 *                         ГЛАВНАЯ ИНИЦИАЛИЗАЦИЯ
 * ============================================================================
 */

int apic_init(void) {
    console_print("\n=== APIC INITIALIZATION ===\n\n");
    
    /* Проверяем поддержку APIC */
    if (!cpu_has_apic()) {
        console_print("ERROR: CPU does not support APIC!\n");
        return -1;
    }
    console_print("CPU supports APIC: YES\n\n");
    
    /* Отключаем legacy PIC */
    pic_disable();
    console_print("\n");
    
    /* Инициализируем Local APIC */
    lapic_init();
    console_print("\n");
    
    /* Инициализируем I/O APIC */
    ioapic_init();
    console_print("\n");
    
    /* Инициализируем клавиатуру */
    keyboard_init();
    
    console_print("\n=== APIC READY ===\n");
    
    return 0;
}

/*
 * ============================================================================
 *                         ОТЛАДКА
 * ============================================================================
 */

void apic_debug_print(void) {
    console_print("\n=== APIC DEBUG INFO ===\n");
    
    /* Local APIC */
    console_print("Local APIC:\n");
    console_print("  ID:      0x");
    console_print_hex(lapic_read(LAPIC_ID));
    console_print("\n");
    console_print("  Version: 0x");
    console_print_hex(lapic_read(LAPIC_VERSION));
    console_print("\n");
    console_print("  SVR:     0x");
    console_print_hex(lapic_read(LAPIC_SVR));
    console_print("\n");
    console_print("  TPR:     0x");
    console_print_hex(lapic_read(LAPIC_TPR));
    console_print("\n");
    console_print("  ISR[0]:  0x");
    console_print_hex(lapic_read(LAPIC_ISR));
    console_print("\n");
    console_print("  IRR[0]:  0x");
    console_print_hex(lapic_read(LAPIC_IRR));
    console_print("\n");
    
    /* I/O APIC Redirection entry для IRQ1 */
    console_print("\nI/O APIC Keyboard entry:\n");
    uint32_t lo = ioapic_read(IOAPIC_REDTBL + IRQ_KEYBOARD * 2);
    uint32_t hi = ioapic_read(IOAPIC_REDTBL + IRQ_KEYBOARD * 2 + 1);
    console_print("  Low:  0x");
    console_print_hex(lo);
    console_print("\n");
    console_print("  High: 0x");
    console_print_hex(hi);
    console_print("\n");
    console_print("  Masked: ");
    console_print((lo & IOAPIC_INT_MASKED) ? "YES" : "NO");
    console_print("\n");
    console_print("  Vector: ");
    console_print_hex(lo & 0xFF);
    console_print("\n");
}