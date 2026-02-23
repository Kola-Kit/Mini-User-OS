#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// Флаги страниц
// ============================================================================

#define PAGE_PRESENT        (1ULL << 0)
#define PAGE_WRITABLE       (1ULL << 1)
#define PAGE_USER           (1ULL << 2)
#define PAGE_WRITETHROUGH   (1ULL << 3)
#define PAGE_NOCACHE        (1ULL << 4)
#define PAGE_ACCESSED       (1ULL << 5)
#define PAGE_DIRTY          (1ULL << 6)
#define PAGE_LARGE          (1ULL << 7)   // PS bit для 2MB/1GB страниц
#define PAGE_GLOBAL         (1ULL << 8)
#define PAGE_NX             (1ULL << 63)  // No Execute

// Маски для извлечения адресов
#define PAGE_ADDR_MASK_4K   0x000FFFFFFFFFF000ULL
#define PAGE_ADDR_MASK_2M   0x000FFFFFFFE00000ULL
#define PAGE_ADDR_MASK_1G   0x000FFFFFC0000000ULL

// Размеры страниц
#define PAGE_SIZE_4K        0x1000ULL
#define PAGE_SIZE_2M        0x200000ULL
#define PAGE_SIZE_1G        0x40000000ULL

// Виртуальные адреса ядра
#define KERNEL_VMA          0xFFFFFFFF80000000ULL
#define KERNEL_VMA_OFFSET   0xFFFF800000000000ULL

// ============================================================================
// Структуры
// ============================================================================

typedef struct {
    uint64_t *pml4;
    uint64_t total_mapped;
    uint64_t tables_allocated;
} paging_context_t;

extern paging_context_t g_kernel_paging;

// ============================================================================
// Основные функции
// ============================================================================

// Инициализация
void paging_init(void);

uint64_t virt_to_phys(void *pml4, uint64_t virt);
// Управление CR3
void paging_load_cr3(uint64_t pml4_phys);
uint64_t paging_read_cr3(void);
void paging_flush_tlb(void);
void paging_invalidate_page(uint64_t virt);

// Маппинг страниц
bool paging_map_page(uint64_t *pml4, uint64_t virt, uint64_t phys, uint64_t flags);
bool paging_map_2mb(uint64_t *pml4, uint64_t virt, uint64_t phys, uint64_t flags);
bool paging_map_1gb(uint64_t *pml4, uint64_t virt, uint64_t phys, uint64_t flags);

// Размаппинг
bool paging_unmap_page(uint64_t *pml4, uint64_t virt);
bool paging_unmap_2mb(uint64_t *pml4, uint64_t virt);

// Маппинг диапазонов
bool paging_map_range(uint64_t *pml4, uint64_t virt_start, uint64_t phys_start, 
                      uint64_t size, uint64_t flags);
bool paging_map_range_2mb(uint64_t *pml4, uint64_t virt_start, uint64_t phys_start,
                          uint64_t size, uint64_t flags);
bool paging_identity_map_range(uint64_t *pml4, uint64_t start, uint64_t size, uint64_t flags);

// Запросы
bool paging_is_mapped(uint64_t *pml4, uint64_t virt);
uint64_t paging_get_phys(uint64_t *pml4, uint64_t virt);
uint64_t paging_get_flags(uint64_t *pml4, uint64_t virt);

// Изменение флагов
bool paging_set_flags(uint64_t *pml4, uint64_t virt, uint64_t flags);
bool paging_protect_range(uint64_t *pml4, uint64_t virt_start, uint64_t size, uint64_t flags);

// Создание/клонирование адресных пространств
uint64_t *paging_create_address_space(void);
uint64_t *paging_clone_address_space(uint64_t *src_pml4);
void paging_destroy_address_space(uint64_t *pml4);
void paging_switch_address_space(uint64_t *pml4);

// Отладка
void paging_dump_tables(uint64_t *pml4, int max_entries);
void paging_print_stats(void);

#endif // PAGING_H