// src/main.c

#include <efi.h>
#include <efilib.h>
#include <stdint.h>
#include <x86_64/efibind.h>

#include "kernel.h"
#include "Initialize/systemTables.h"
#include "Tables/gdt.h"
#include "Tables/idt.h"
#include "Root/apic.h"
#include "Root/io.h"
#include "Root/uefiSetting.h"
#include "Drivers/keyboard.h"
#include "Drivers/timer.h"
#include "MemoryManager/uefiMapParser.h"
#include "MemoryManager/pmm.h"
#include "MemoryManager/paging.h"
#include "MemoryManager/vmm.h"
#include "MemoryManager/heap.h"

extern uint8_t __bss_start[];
extern uint8_t __bss_end[];
extern uint8_t __kernel_start[];
extern uint8_t __kernel_end[];

__attribute__((section(".bss"), aligned(16)))
static uint8_t stack[65536];

extern void console_set_color(uint32_t fg, uint32_t bg);
extern void console_clear(void);
extern void console_putchar(char c);
extern void console_print(const char *str);
extern void console_print_hex(uint64_t val);
extern void console_print_dec(uint64_t val);
extern void console_print_int(int64_t val);
extern void enable_output(void);
extern void disable_output(void);

extern UINT32 MakeColor(UINT8 R, UINT8 G, UINT8 B);

void kernel_main(void) {
    __asm__ volatile (
        "cli\n"
        "lea %[stack_top], %%rsp\n"
        "xor %%rbp, %%rbp\n"
        "pushq $0\n"
        "popfq\n"
        "cld\n"
        "lea %[bss_start], %%rdi\n"
        "lea %[bss_end], %%rcx\n"
        "sub %%rdi, %%rcx\n"
        "shr $3, %%rcx\n"
        "xor %%eax, %%eax\n"
        "rep stosq\n"
        "and $~0xF, %%rsp\n"
        :
        : [stack_top]  "m" (stack[sizeof(stack)]),
          [bss_start]  "m" (*__bss_start),
          [bss_end]    "m" (*__bss_end)
        : "memory", "rax", "rcx", "rdi"
    );

    // Шаг 1: Базовые таблицы
    gdt_init();
    console_print("[BOOT] GDT initialized\n");
    
    idt_init();
    console_print("[BOOT] IDT initialized\n");

    // Шаг 2: Память (нужна ДО APIC для paging)
    ParseMemoryMap();
    console_print("[BOOT] Memory map parsed\n");
    
    pmm_init();
    console_print("[BOOT] PMM initialized\n");
    
    // Шаг 3: Paging — ПЕРЕД APIC init, чтобы MMIO было корректно замаплено
    paging_init();
    console_print("[BOOT] Paging initialized\n");

    // Шаг 4: VMM и Heap
    vmm_init();
    heap_init();

    UefiVirtualMapSettingUp();

    // Шаг 5: APIC — ПОСЛЕ paging, чтобы MMIO работало через новые таблицы
    apic_init();
    
    // Шаг 6: Драйверы
    keyboard_driver_init();
    idt_register_handler(33, keyboard_driver_handler);

    // Шаг 7: Таймер (теперь используем APIC Timer)
    timer_init(1000);

    // Шаг 8: Включаем прерывания
    console_print("\n[BOOT] Enabling interrupts...\n");
    interrupts_enable();

    console_set_color(MakeColor(0, 255, 0), MakeColor(0, 0, 0));
    console_print("MiniUserOS is booted!\n");
    console_set_color(MakeColor(255, 255, 255), MakeColor(0, 0, 0));

    while (1) {
        __asm__("hlt");
    }
}
