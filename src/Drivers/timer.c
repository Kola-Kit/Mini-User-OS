// src/Drivers/timer.c

#include "Drivers/timer.h"
#include "Tables/idt.h"
#include "Root/io.h"
#include "Root/apic.h"

extern void console_putchar(char c);
extern void console_print(const char *str);
extern void console_print_hex(uint64_t val);
extern void console_print_dec(uint64_t val);

static volatile uint64_t timer_ticks = 0;

/*
 * Метод 1: Калибровка через PIT Channel 2 (классический)
 * Возвращает 0 при неудаче
 */
static uint32_t calibrate_via_pit_ch2(void) {
    uint16_t pit_count = 11932;  // ~10ms при 1193182 Hz
    
    // Настраиваем PIT Channel 2
    uint8_t port61 = inb(0x61);
    outb(0x61, (port61 & 0xFD) | 0x01);
    outb(0x43, 0xB0);  // Channel 2, lobyte/hibyte, mode 0
    outb(0x42, pit_count & 0xFF);
    outb(0x42, (pit_count >> 8) & 0xFF);
    
    // Перезапуск Gate
    uint8_t tmp = inb(0x61);
    outb(0x61, tmp & 0xFE);
    outb(0x61, tmp | 0x01);
    
    // Запускаем APIC Timer
    lapic_write(LAPIC_TIMER_DCR, 0x03);
    lapic_write(LAPIC_TIMER_ICR, 0xFFFFFFFF);
    
    // Ждём с ТАЙМАУТОМ
    // На ~3 GHz процессоре 100 миллионов итераций ≈ 30-50ms
    // Если PIT Channel 2 работает, 10ms пройдут за ~30M итераций
    volatile uint32_t timeout = 100000000;
    while (!(inb(0x61) & 0x20)) {
        if (--timeout == 0) {
            // PIT Channel 2 не работает на этой платформе!
            lapic_write(LAPIC_TIMER, LAPIC_LVT_MASKED);
            console_print("[TIMER] PIT Ch2 timeout - not supported\n");
            
            // Восстанавливаем порт 0x61
            outb(0x61, port61);
            return 0;  // Неудача
        }
    }
    
    lapic_write(LAPIC_TIMER, LAPIC_LVT_MASKED);
    uint32_t apic_ticks = 0xFFFFFFFF - lapic_read(LAPIC_TIMER_CCR);
    
    // Восстанавливаем порт 0x61
    outb(0x61, port61);
    
    return apic_ticks;
}

/*
 * Метод 2: Калибровка через PIT Channel 0 (более надёжный)
 * PIT Channel 0 работает практически везде
 */
static uint32_t calibrate_via_pit_ch0(void) {
    // Используем PIT Channel 0 в mode 0 (interrupt on terminal count)
    // Считаем ~10ms = 11932 тиков PIT
    uint16_t pit_count = 11932;
    
    // Настраиваем PIT Channel 0: mode 0, lobyte/hibyte
    outb(0x43, 0x30);  // Channel 0, lobyte/hibyte, mode 0, binary
    outb(0x40, pit_count & 0xFF);
    outb(0x40, (pit_count >> 8) & 0xFF);
    
    // Запускаем APIC Timer
    lapic_write(LAPIC_TIMER_DCR, 0x03);  // Divider = 16
    lapic_write(LAPIC_TIMER_ICR, 0xFFFFFFFF);
    
    // Читаем PIT counter до тех пор, пока он не обнулится
    // PIT Channel 0 ВСЕГДА читается через latch command
    uint16_t prev_count = pit_count;
    uint32_t timeout = 100000000;
    
    while (timeout-- > 0) {
        // Latch command для Channel 0
        outb(0x43, 0x00);  // Latch Channel 0
        uint8_t lo = inb(0x40);
        uint8_t hi = inb(0x40);
        uint16_t current = (uint16_t)hi << 8 | lo;
        
        // Mode 0: счётчик считает вниз от pit_count до 0
        // Когда достигает 0, OUT goes high
        // Но нам нужно просто проверить что он обернулся
        if (current > prev_count) {
            // Счётчик обернулся через 0 — прошло ~10ms
            break;
        }
        prev_count = current;
    }
    
    lapic_write(LAPIC_TIMER, LAPIC_LVT_MASKED);
    uint32_t apic_ticks = 0xFFFFFFFF - lapic_read(LAPIC_TIMER_CCR);
    
    if (timeout == 0) {
        console_print("[TIMER] PIT Ch0 calibration timeout\n");
        return 0;
    }
    
    return apic_ticks;
}

/*
 * Метод 3: Калибровка через TSC (самый надёжный на современных CPU)
 * Использует CPUID для получения частоты TSC, если доступно
 */
static uint32_t calibrate_via_tsc(void) {
    // Проверяем поддержку Invariant TSC
    uint32_t eax, ebx, ecx, edx;
    
    // Проверяем CPUID leaf 0x80000007 (Advanced Power Management)
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) 
                     : "a"(0x80000007));
    
    int has_invariant_tsc = (edx >> 8) & 1;
    if (!has_invariant_tsc) {
        console_print("[TIMER] No invariant TSC\n");
        return 0;
    }
    
    // Пробуем CPUID leaf 0x15 (Time Stamp Counter and Core Crystal Clock)
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) 
                     : "a"(0x15));
    
    if (eax != 0 && ebx != 0 && ecx != 0) {
        // TSC freq = ecx * ebx / eax
        uint64_t tsc_freq = ((uint64_t)ecx * ebx) / eax;
        console_print("[TIMER] TSC frequency from CPUID: ");
        console_print_dec(tsc_freq / 1000000);
        console_print(" MHz\n");
        
        // Запускаем APIC Timer и замеряем через TSC
        lapic_write(LAPIC_TIMER_DCR, 0x03);
        lapic_write(LAPIC_TIMER_ICR, 0xFFFFFFFF);
        
        // Ждём 10ms через TSC
        uint64_t tsc_start, tsc_end;
        uint64_t tsc_wait = tsc_freq / 100;  // 10ms
        
        __asm__ volatile("rdtsc" : "=a"(eax), "=d"(edx));
        tsc_start = ((uint64_t)edx << 32) | eax;
        
        do {
            __asm__ volatile("rdtsc" : "=a"(eax), "=d"(edx));
            tsc_end = ((uint64_t)edx << 32) | eax;
        } while (tsc_end - tsc_start < tsc_wait);
        
        lapic_write(LAPIC_TIMER, LAPIC_LVT_MASKED);
        return 0xFFFFFFFF - lapic_read(LAPIC_TIMER_CCR);
    }
    
    // CPUID 0x15 не поддерживается, пробуем грубую оценку через TSC
    // Используем короткий busy-wait цикл
    console_print("[TIMER] TSC CPUID leaf 0x15 not available\n");
    return 0;
}

/*
 * Основная калибровка — пробуем несколько методов
 */
static uint32_t calibrate_apic_timer(void) {
    uint32_t ticks = 0;
    
    // Метод 1: TSC (самый надёжный на современных CPU)
    console_print("[TIMER] Trying TSC calibration...\n");
    ticks = calibrate_via_tsc();
    if (ticks > 0) {
        console_print("[TIMER] TSC calibration OK: ");
        console_print_dec(ticks);
        console_print(" ticks/10ms\n");
        return ticks;
    }
    
    // Метод 2: PIT Channel 2 (классический, но не везде работает)
    console_print("[TIMER] Trying PIT Channel 2 calibration...\n");
    ticks = calibrate_via_pit_ch2();
    if (ticks > 0) {
        console_print("[TIMER] PIT Ch2 calibration OK: ");
        console_print_dec(ticks);
        console_print(" ticks/10ms\n");
        return ticks;
    }
    
    // Метод 3: PIT Channel 0 (запасной)
    console_print("[TIMER] Trying PIT Channel 0 calibration...\n");
    ticks = calibrate_via_pit_ch0();
    if (ticks > 0) {
        console_print("[TIMER] PIT Ch0 calibration OK: ");
        console_print_dec(ticks);
        console_print(" ticks/10ms\n");
        return ticks;
    }
    
    // Ничего не сработало — используем эвристику
    console_print("[TIMER] WARNING: All calibration methods failed!\n");
    console_print("[TIMER] Using fallback value\n");
    
    // Грубая оценка: на ~3GHz CPU с divider=16, 
    // APIC Timer тикает ~187.5 MHz
    // 10ms = 1875000 тиков
    return 1875000;
}

void timer_init(int frequency) {
    console_print("\n[TIMER] === Initializing APIC Timer ===\n");
    
    idt_register_handler(32, timer_handler);
    
    uint32_t ticks_per_10ms = calibrate_apic_timer();
    
    uint32_t initial_count = (ticks_per_10ms * 100) / frequency;
    if (initial_count == 0) initial_count = 1;
    
    console_print("[TIMER] Frequency: ");
    console_print_dec(frequency);
    console_print(" Hz\n");
    console_print("[TIMER] Initial count: ");
    console_print_dec(initial_count);
    console_print("\n");
    
    // Настраиваем APIC Timer: вектор 32, периодический режим
    lapic_write(LAPIC_TIMER_DCR, 0x03);             // Divider = 16
    lapic_write(LAPIC_TIMER, 32 | (1 << 17));        // Periodic mode
    lapic_write(LAPIC_TIMER_ICR, initial_count);      // Запуск!
    
    console_print("[TIMER] APIC Timer running!\n\n");
}

void timer_handler(interrupt_frame_t* frame) {
    (void)frame;
    timer_ticks++;
    
    if (timer_ticks % 1000 == 0) {
        console_putchar('T');
    }
    
    lapic_send_eoi();
}

uint64_t timer_get_ticks(void) {
    return timer_ticks;
}
