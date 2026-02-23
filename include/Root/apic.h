#ifndef APIC_H
#define APIC_H

#include <stdint.h>

/*
 * ============================================================================
 *                         LOCAL APIC РЕГИСТРЫ
 * ============================================================================
 * 
 * Local APIC маппится в память по адресу из MSR 0x1B (обычно 0xFEE00000)
 * Каждый регистр занимает 16 байт (выровнен на 16), но используются только 4
 */

/* Смещения регистров Local APIC */
#define LAPIC_ID            0x020   /* Local APIC ID */
#define LAPIC_VERSION       0x030   /* Version */
#define LAPIC_TPR           0x080   /* Task Priority Register */
#define LAPIC_APR           0x090   /* Arbitration Priority */
#define LAPIC_PPR           0x0A0   /* Processor Priority */
#define LAPIC_EOI           0x0B0   /* End of Interrupt */
#define LAPIC_RRD           0x0C0   /* Remote Read */
#define LAPIC_LDR           0x0D0   /* Logical Destination */
#define LAPIC_DFR           0x0E0   /* Destination Format */
#define LAPIC_SVR           0x0F0   /* Spurious Interrupt Vector */
#define LAPIC_ISR           0x100   /* In-Service Register (8 x 32-bit) */
#define LAPIC_TMR           0x180   /* Trigger Mode Register */
#define LAPIC_IRR           0x200   /* Interrupt Request Register */
#define LAPIC_ESR           0x280   /* Error Status Register */
#define LAPIC_ICRLO         0x300   /* Interrupt Command Register (low) */
#define LAPIC_ICRHI         0x310   /* Interrupt Command Register (high) */
#define LAPIC_TIMER         0x320   /* Timer LVT */
#define LAPIC_THERMAL       0x330   /* Thermal LVT */
#define LAPIC_PERF          0x340   /* Performance Counter LVT */
#define LAPIC_LINT0         0x350   /* Local Interrupt 0 LVT */
#define LAPIC_LINT1         0x360   /* Local Interrupt 1 LVT */
#define LAPIC_ERROR         0x370   /* Error LVT */
#define LAPIC_TIMER_ICR     0x380   /* Timer Initial Count */
#define LAPIC_TIMER_CCR     0x390   /* Timer Current Count */
#define LAPIC_TIMER_DCR     0x3E0   /* Timer Divide Configuration */

/* SVR биты */
#define LAPIC_SVR_ENABLE    0x100   /* APIC Software Enable */

/* LVT биты */
#define LAPIC_LVT_MASKED    0x10000 /* Interrupt masked */

/*
 * ============================================================================
 *                         I/O APIC РЕГИСТРЫ
 * ============================================================================
 * 
 * I/O APIC обычно находится по адресу 0xFEC00000
 * Доступ через два регистра: IOREGSEL (выбор) и IOWIN (данные)
 */

#define IOAPIC_BASE         0xFEC00000

/* Регистры доступа */
#define IOAPIC_REGSEL       0x00    /* Register Select (write) */
#define IOAPIC_IOWIN        0x10    /* I/O Window (read/write) */

/* Индексы регистров */
#define IOAPIC_ID           0x00    /* I/O APIC ID */
#define IOAPIC_VER          0x01    /* Version */
#define IOAPIC_ARB          0x02    /* Arbitration ID */
#define IOAPIC_REDTBL       0x10    /* Redirection Table (0x10 + 2*n) */

/* Redirection Entry биты */
#define IOAPIC_INT_MASKED   (1 << 16)
#define IOAPIC_INT_LEVEL    (1 << 15)   /* Level triggered */
#define IOAPIC_INT_EDGE     (0 << 15)   /* Edge triggered */
#define IOAPIC_INT_LOW      (1 << 13)   /* Active low */
#define IOAPIC_INT_HIGH     (0 << 13)   /* Active high */
#define IOAPIC_INT_LOGICAL  (1 << 11)   /* Logical destination */
#define IOAPIC_INT_PHYSICAL (0 << 11)   /* Physical destination */
#define IOAPIC_INT_FIXED    (0 << 8)    /* Fixed delivery */
#define IOAPIC_INT_LOWEST   (1 << 8)    /* Lowest priority */

/*
 * ============================================================================
 *                         IRQ -> GSI МАППИНГ
 * ============================================================================
 */

/* Стандартный маппинг ISA IRQ на GSI (Global System Interrupt) */
#define IRQ_TIMER           0
#define IRQ_KEYBOARD        1
#define IRQ_CASCADE         2
#define IRQ_COM2            3
#define IRQ_COM1            4
#define IRQ_LPT2            5
#define IRQ_FLOPPY          6
#define IRQ_LPT1            7
#define IRQ_RTC             8
#define IRQ_ACPI            9
#define IRQ_OPEN1           10
#define IRQ_OPEN2           11
#define IRQ_MOUSE           12
#define IRQ_FPU             13
#define IRQ_ATA1            14
#define IRQ_ATA2            15

/* Векторы прерываний (можно выбрать любые >= 32) */
#define IRQ_BASE_VECTOR     32

/*
 * ============================================================================
 *                         ФУНКЦИИ
 * ============================================================================
 */

/* Инициализация */
int apic_init(void);

/* Local APIC */
void lapic_write(uint32_t reg, uint32_t value);
uint32_t lapic_read(uint32_t reg);
void lapic_send_eoi(void);
uint8_t lapic_get_id(void);

/* I/O APIC */
void ioapic_write(uint8_t reg, uint32_t value);
uint32_t ioapic_read(uint8_t reg);
void ioapic_set_irq(uint8_t irq, uint8_t vector, uint8_t dest_cpu, uint32_t flags);
void ioapic_mask_irq(uint8_t irq);
void ioapic_unmask_irq(uint8_t irq);

/* Отладка */
void apic_debug_print(void);

#endif /* APIC_H */
