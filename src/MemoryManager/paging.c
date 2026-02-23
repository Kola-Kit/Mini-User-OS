#include <efi.h>
#include <efilib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include "kernel.h"
#include "Initialize/baseGraphic.h"
#include "MemoryManager/paging.h"
#include "MemoryManager/pmm.h"
#include "MemoryManager/uefiMapParser.h"

// ============================================================================
// Внешние функции
// ============================================================================

extern void console_print(const char* str);
extern void console_print_hex(uint64_t val);
extern void console_print_dec(uint64_t val);

// ============================================================================
// Глобальные переменные
// ============================================================================

paging_context_t g_kernel_paging = {
    .pml4 = NULL,
    .total_mapped = 0,
    .tables_allocated = 0
};

// ============================================================================
// Низкоуровневые функции CR3/TLB
// ============================================================================

void paging_load_cr3(uint64_t pml4_phys) {
    __asm__ volatile("mov %0, %%cr3" : : "r"(pml4_phys) : "memory");
}

uint64_t paging_read_cr3(void) {
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3 & PAGE_ADDR_MASK_4K;
}

void paging_flush_tlb(void) {
    paging_load_cr3(paging_read_cr3());
}

void paging_invalidate_page(uint64_t virt) {
    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
}

// ============================================================================
// Вспомогательные функции
// ============================================================================

static void zero_page(uint64_t *page) {
    for (int i = 0; i < 512; i++) {
        page[i] = 0;
    }
}

static inline uint64_t get_pml4_index(uint64_t virt) {
    return (virt >> 39) & 0x1FF;
}

static inline uint64_t get_pdpt_index(uint64_t virt) {
    return (virt >> 30) & 0x1FF;
}

static inline uint64_t get_pd_index(uint64_t virt) {
    return (virt >> 21) & 0x1FF;
}

static inline uint64_t get_pt_index(uint64_t virt) {
    return (virt >> 12) & 0x1FF;
}

static inline uint64_t* get_table_ptr(uint64_t entry) {
    return (uint64_t*)(entry & PAGE_ADDR_MASK_4K);
}

static inline bool is_present(uint64_t entry) {
    return (entry & PAGE_PRESENT) != 0;
}

static inline bool is_large_page(uint64_t entry) {
    return (entry & PAGE_LARGE) != 0;
}

// Разбить 2MB страницу на 512 x 4KB страниц
static uint64_t* split_2mb_page(uint64_t *pd, uint64_t pd_index) {
    uint64_t old_entry = pd[pd_index];
    
    if (!is_present(old_entry) || !is_large_page(old_entry)) {
        return NULL;
    }
    
    // Получаем базовый физический адрес 2MB страницы
    uint64_t phys_base = old_entry & PAGE_ADDR_MASK_2M;
    uint64_t flags = old_entry & 0xFFF & ~PAGE_LARGE;  // Убираем PS бит
    
    // Выделяем новую PT
    uint64_t *pt = (uint64_t*)pmm_alloc_page();
    if (!pt) {
        return NULL;
    }
    
    g_kernel_paging.tables_allocated++;
    
    // Заполняем PT 512-ю 4KB страницами
    for (int i = 0; i < 512; i++) {
        pt[i] = (phys_base + i * PAGE_SIZE_4K) | flags | PAGE_PRESENT;
    }
    
    // Заменяем 2MB запись на указатель на PT
    pd[pd_index] = (uint64_t)pt | PAGE_PRESENT | PAGE_WRITABLE | (flags & PAGE_USER);
    
    return pt;
}

// Получить или создать таблицу следующего уровня
static uint64_t* get_or_create_table(uint64_t *table, uint64_t index, uint64_t flags) {
    if (is_present(table[index])) {
        if (is_large_page(table[index])) {
            // Это не должно случаться для PML4 и PDPT в нашем случае
            return NULL;
        }
        return get_table_ptr(table[index]);
    }
    
    // Выделяем новую таблицу
    uint64_t *new_table = (uint64_t*)pmm_alloc_page();
    if (!new_table) {
        return NULL;
    }
    
    zero_page(new_table);
    g_kernel_paging.tables_allocated++;
    
    table[index] = (uint64_t)new_table | PAGE_PRESENT | PAGE_WRITABLE | (flags & PAGE_USER);
    
    return new_table;
}

// Получить или создать PT, разбивая 2MB страницу если нужно
static uint64_t* get_or_create_pt(uint64_t *pml4, uint64_t virt, uint64_t flags) {
    uint64_t pml4_idx = get_pml4_index(virt);
    uint64_t pdpt_idx = get_pdpt_index(virt);
    uint64_t pd_idx   = get_pd_index(virt);
    
    // PML4 -> PDPT
    uint64_t *pdpt = get_or_create_table(pml4, pml4_idx, flags);
    if (!pdpt) return NULL;
    
    // Проверяем 1GB страницу
    if (is_present(pdpt[pdpt_idx]) && is_large_page(pdpt[pdpt_idx])) {
        // 1GB страницу разбивать сложнее, пока не поддерживаем
        return NULL;
    }
    
    // PDPT -> PD
    uint64_t *pd = get_or_create_table(pdpt, pdpt_idx, flags);
    if (!pd) return NULL;
    
    // Проверяем 2MB страницу
    if (is_present(pd[pd_idx]) && is_large_page(pd[pd_idx])) {
        // Разбиваем 2MB на 4KB
        uint64_t *pt = split_2mb_page(pd, pd_idx);
        if (!pt) return NULL;
        return pt;
    }
    
    // PD -> PT
    return get_or_create_table(pd, pd_idx, flags);
}

// Получить таблицу без создания
static uint64_t* get_table(uint64_t *table, uint64_t index) {
    if (!is_present(table[index])) {
        return NULL;
    }
    if (is_large_page(table[index])) {
        return NULL;
    }
    return get_table_ptr(table[index]);
}

// ============================================================================
// Маппинг страниц
// ============================================================================

bool paging_map_page(uint64_t *pml4, uint64_t virt, uint64_t phys, uint64_t flags) {
    if (!pml4) pml4 = g_kernel_paging.pml4;
    if (!pml4) return false;
    
    uint64_t *pt = get_or_create_pt(pml4, virt, flags);
    if (!pt) return false;
    
    uint64_t pt_idx = get_pt_index(virt);
    pt[pt_idx] = (phys & PAGE_ADDR_MASK_4K) | flags | PAGE_PRESENT;
    
    g_kernel_paging.total_mapped += PAGE_SIZE_4K;
    
    return true;
}

bool paging_map_2mb(uint64_t *pml4, uint64_t virt, uint64_t phys, uint64_t flags) {
    if (!pml4) pml4 = g_kernel_paging.pml4;
    if (!pml4) return false;
    
    if ((virt & (PAGE_SIZE_2M - 1)) || (phys & (PAGE_SIZE_2M - 1))) {
        return false;
    }
    
    uint64_t pml4_idx = get_pml4_index(virt);
    uint64_t pdpt_idx = get_pdpt_index(virt);
    uint64_t pd_idx   = get_pd_index(virt);
    
    uint64_t *pdpt = get_or_create_table(pml4, pml4_idx, flags);
    if (!pdpt) return false;
    
    uint64_t *pd = get_or_create_table(pdpt, pdpt_idx, flags);
    if (!pd) return false;
    
    pd[pd_idx] = (phys & PAGE_ADDR_MASK_2M) | flags | PAGE_PRESENT | PAGE_LARGE;
    
    g_kernel_paging.total_mapped += PAGE_SIZE_2M;
    
    return true;
}

bool paging_map_1gb(uint64_t *pml4, uint64_t virt, uint64_t phys, uint64_t flags) {
    if (!pml4) pml4 = g_kernel_paging.pml4;
    if (!pml4) return false;
    
    if ((virt & (PAGE_SIZE_1G - 1)) || (phys & (PAGE_SIZE_1G - 1))) {
        return false;
    }
    
    uint64_t pml4_idx = get_pml4_index(virt);
    uint64_t pdpt_idx = get_pdpt_index(virt);
    
    uint64_t *pdpt = get_or_create_table(pml4, pml4_idx, flags);
    if (!pdpt) return false;
    
    pdpt[pdpt_idx] = (phys & PAGE_ADDR_MASK_1G) | flags | PAGE_PRESENT | PAGE_LARGE;
    
    g_kernel_paging.total_mapped += PAGE_SIZE_1G;
    
    return true;
}

// ============================================================================
// Размаппинг
// ============================================================================

bool paging_unmap_page(uint64_t *pml4, uint64_t virt) {
    if (!pml4) pml4 = g_kernel_paging.pml4;
    if (!pml4) return false;
    
    uint64_t *pdpt = get_table(pml4, get_pml4_index(virt));
    if (!pdpt) return false;
    
    uint64_t *pd = get_table(pdpt, get_pdpt_index(virt));
    if (!pd) return false;
    
    uint64_t *pt = get_table(pd, get_pd_index(virt));
    if (!pt) return false;
    
    uint64_t pt_idx = get_pt_index(virt);
    if (!is_present(pt[pt_idx])) return false;
    
    pt[pt_idx] = 0;
    paging_invalidate_page(virt);
    
    return true;
}

bool paging_unmap_2mb(uint64_t *pml4, uint64_t virt) {
    if (!pml4) pml4 = g_kernel_paging.pml4;
    if (!pml4) return false;
    
    uint64_t *pdpt = get_table(pml4, get_pml4_index(virt));
    if (!pdpt) return false;
    
    uint64_t *pd = get_table(pdpt, get_pdpt_index(virt));
    if (!pd) return false;
    
    uint64_t pd_idx = get_pd_index(virt);
    if (!is_present(pd[pd_idx]) || !is_large_page(pd[pd_idx])) return false;
    
    pd[pd_idx] = 0;
    paging_invalidate_page(virt);
    
    return true;
}

// ============================================================================
// Маппинг диапазонов
// ============================================================================

bool paging_map_range(uint64_t *pml4, uint64_t virt_start, uint64_t phys_start,
                      uint64_t size, uint64_t flags) {
    uint64_t virt = virt_start;
    uint64_t phys = phys_start;
    uint64_t end = virt_start + size;
    
    while (virt < end) {
        if (!paging_map_page(pml4, virt, phys, flags)) {
            return false;
        }
        virt += PAGE_SIZE_4K;
        phys += PAGE_SIZE_4K;
    }
    
    return true;
}

bool paging_map_range_2mb(uint64_t *pml4, uint64_t virt_start, uint64_t phys_start,
                          uint64_t size, uint64_t flags) {
    uint64_t virt = virt_start & ~(PAGE_SIZE_2M - 1);
    uint64_t phys = phys_start & ~(PAGE_SIZE_2M - 1);
    uint64_t end = (virt_start + size + PAGE_SIZE_2M - 1) & ~(PAGE_SIZE_2M - 1);
    
    while (virt < end) {
        if (!paging_map_2mb(pml4, virt, phys, flags)) {
            return false;
        }
        virt += PAGE_SIZE_2M;
        phys += PAGE_SIZE_2M;
    }
    
    return true;
}

bool paging_identity_map_range(uint64_t *pml4, uint64_t start, uint64_t size, uint64_t flags) {
    return paging_map_range_2mb(pml4, start, start, size, flags);
}

// ============================================================================
// Запросы
// ============================================================================

bool paging_is_mapped(uint64_t *pml4, uint64_t virt) {
    if (!pml4) pml4 = g_kernel_paging.pml4;
    if (!pml4) return false;
    
    uint64_t pml4_idx = get_pml4_index(virt);
    if (!is_present(pml4[pml4_idx])) return false;
    
    uint64_t *pdpt = get_table_ptr(pml4[pml4_idx]);
    uint64_t pdpt_idx = get_pdpt_index(virt);
    if (!is_present(pdpt[pdpt_idx])) return false;
    if (is_large_page(pdpt[pdpt_idx])) return true;
    
    uint64_t *pd = get_table_ptr(pdpt[pdpt_idx]);
    uint64_t pd_idx = get_pd_index(virt);
    if (!is_present(pd[pd_idx])) return false;
    if (is_large_page(pd[pd_idx])) return true;
    
    uint64_t *pt = get_table_ptr(pd[pd_idx]);
    uint64_t pt_idx = get_pt_index(virt);
    
    return is_present(pt[pt_idx]);
}

uint64_t paging_get_phys(uint64_t *pml4, uint64_t virt) {
    if (!pml4) pml4 = g_kernel_paging.pml4;
    if (!pml4) return 0;
    
    uint64_t pml4_idx = get_pml4_index(virt);
    if (!is_present(pml4[pml4_idx])) return 0;
    
    uint64_t *pdpt = get_table_ptr(pml4[pml4_idx]);
    uint64_t pdpt_idx = get_pdpt_index(virt);
    if (!is_present(pdpt[pdpt_idx])) return 0;
    
    if (is_large_page(pdpt[pdpt_idx])) {
        return (pdpt[pdpt_idx] & PAGE_ADDR_MASK_1G) | (virt & (PAGE_SIZE_1G - 1));
    }
    
    uint64_t *pd = get_table_ptr(pdpt[pdpt_idx]);
    uint64_t pd_idx = get_pd_index(virt);
    if (!is_present(pd[pd_idx])) return 0;
    
    if (is_large_page(pd[pd_idx])) {
        return (pd[pd_idx] & PAGE_ADDR_MASK_2M) | (virt & (PAGE_SIZE_2M - 1));
    }
    
    uint64_t *pt = get_table_ptr(pd[pd_idx]);
    uint64_t pt_idx = get_pt_index(virt);
    if (!is_present(pt[pt_idx])) return 0;
    
    return (pt[pt_idx] & PAGE_ADDR_MASK_4K) | (virt & (PAGE_SIZE_4K - 1));
}

uint64_t paging_get_flags(uint64_t *pml4, uint64_t virt) {
    if (!pml4) pml4 = g_kernel_paging.pml4;
    if (!pml4) return 0;
    
    uint64_t pml4_idx = get_pml4_index(virt);
    if (!is_present(pml4[pml4_idx])) return 0;
    
    uint64_t *pdpt = get_table_ptr(pml4[pml4_idx]);
    uint64_t pdpt_idx = get_pdpt_index(virt);
    if (!is_present(pdpt[pdpt_idx])) return 0;
    if (is_large_page(pdpt[pdpt_idx])) return pdpt[pdpt_idx] & 0xFFF;
    
    uint64_t *pd = get_table_ptr(pdpt[pdpt_idx]);
    uint64_t pd_idx = get_pd_index(virt);
    if (!is_present(pd[pd_idx])) return 0;
    if (is_large_page(pd[pd_idx])) return pd[pd_idx] & 0xFFF;
    
    uint64_t *pt = get_table_ptr(pd[pd_idx]);
    uint64_t pt_idx = get_pt_index(virt);
    
    return pt[pt_idx] & 0xFFF;
}

// ============================================================================
// Изменение флагов
// ============================================================================

bool paging_set_flags(uint64_t *pml4, uint64_t virt, uint64_t flags) {
    if (!pml4) pml4 = g_kernel_paging.pml4;
    if (!pml4) return false;
    
    uint64_t *pdpt = get_table(pml4, get_pml4_index(virt));
    if (!pdpt) return false;
    
    uint64_t pdpt_idx = get_pdpt_index(virt);
    if (is_present(pdpt[pdpt_idx]) && is_large_page(pdpt[pdpt_idx])) {
        pdpt[pdpt_idx] = (pdpt[pdpt_idx] & PAGE_ADDR_MASK_1G) | flags | PAGE_PRESENT | PAGE_LARGE;
        paging_invalidate_page(virt);
        return true;
    }
    
    uint64_t *pd = get_table(pdpt, pdpt_idx);
    if (!pd) return false;
    
    uint64_t pd_idx = get_pd_index(virt);
    if (is_present(pd[pd_idx]) && is_large_page(pd[pd_idx])) {
        pd[pd_idx] = (pd[pd_idx] & PAGE_ADDR_MASK_2M) | flags | PAGE_PRESENT | PAGE_LARGE;
        paging_invalidate_page(virt);
        return true;
    }
    
    uint64_t *pt = get_table(pd, pd_idx);
    if (!pt) return false;
    
    uint64_t pt_idx = get_pt_index(virt);
    if (!is_present(pt[pt_idx])) return false;
    
    pt[pt_idx] = (pt[pt_idx] & PAGE_ADDR_MASK_4K) | flags | PAGE_PRESENT;
    paging_invalidate_page(virt);
    
    return true;
}

bool paging_protect_range(uint64_t *pml4, uint64_t virt_start, uint64_t size, uint64_t flags) {
    uint64_t virt = virt_start;
    uint64_t end = virt_start + size;
    
    while (virt < end) {
        paging_set_flags(pml4, virt, flags);
        virt += PAGE_SIZE_4K;
    }
    
    return true;
}

// ============================================================================
// Адресные пространства
// ============================================================================

uint64_t *paging_create_address_space(void) {
    uint64_t *pml4 = (uint64_t*)pmm_alloc_page();
    if (!pml4) return NULL;
    
    zero_page(pml4);
    
    // Копируем верхнюю половину из ядра
    if (g_kernel_paging.pml4) {
        for (int i = 256; i < 512; i++) {
            pml4[i] = g_kernel_paging.pml4[i];
        }
    }
    
    return pml4;
}

static uint64_t* clone_table(uint64_t *src, int level) {
    if (!src) return NULL;
    
    uint64_t *dst = (uint64_t*)pmm_alloc_page();
    if (!dst) return NULL;
    
    zero_page(dst);
    
    for (int i = 0; i < 512; i++) {
        if (!is_present(src[i])) continue;
        
        if (is_large_page(src[i]) || level == 1) {
            dst[i] = src[i];
        } else {
            uint64_t *sub = clone_table(get_table_ptr(src[i]), level - 1);
            if (sub) {
                dst[i] = (uint64_t)sub | (src[i] & 0xFFF);
            }
        }
    }
    
    return dst;
}

uint64_t *paging_clone_address_space(uint64_t *src_pml4) {
    if (!src_pml4) src_pml4 = g_kernel_paging.pml4;
    if (!src_pml4) return NULL;
    
    return clone_table(src_pml4, 4);
}

static void free_table(uint64_t *table, int level) {
    if (!table || level < 1) return;
    
    for (int i = 0; i < 512; i++) {
        if (!is_present(table[i])) continue;
        if (is_large_page(table[i])) continue;
        
        if (level > 1) {
            free_table(get_table_ptr(table[i]), level - 1);
        }
    }
    
    pmm_free_page(table);
}

void paging_destroy_address_space(uint64_t *pml4) {
    if (!pml4 || pml4 == g_kernel_paging.pml4) return;
    
    // Освобождаем только нижнюю половину
    for (int i = 0; i < 256; i++) {
        if (is_present(pml4[i]) && !is_large_page(pml4[i])) {
            free_table(get_table_ptr(pml4[i]), 3);
        }
    }
    
    pmm_free_page(pml4);
}

void paging_switch_address_space(uint64_t *pml4) {
    if (!pml4) return;
    paging_load_cr3((uint64_t)pml4);
}

// ============================================================================
// Инициализация
// ============================================================================

// src/MemoryManager/paging.c

void paging_init(void) {
    console_print("\n[PAGING] === Initializing ===\n");
    
    uint64_t old_cr3 = paging_read_cr3();
    console_print("[PAGING] UEFI CR3: 0x");
    console_print_hex(old_cr3);
    console_print("\n");
    
    extern GRAPHICS_INFO Graphics;
    uint64_t fb_addr = (uint64_t)Graphics.FramebufferBase;
    uint64_t fb_size = Graphics.FramebufferSize;
    
    console_print("[PAGING] Framebuffer: 0x");
    console_print_hex(fb_addr);
    console_print(" size: 0x");
    console_print_hex(fb_size);
    console_print("\n");
    
    uint64_t *pml4 = (uint64_t*)pmm_alloc_page();
    if (!pml4) {
        console_print("[PAGING] FATAL: Cannot allocate PML4!\n");
        return;
    }
    
    zero_page(pml4);
    g_kernel_paging.pml4 = pml4;
    g_kernel_paging.tables_allocated = 1;
    g_kernel_paging.total_mapped = 0;
    
    console_print("[PAGING] New PML4: 0x");
    console_print_hex((uint64_t)pml4);
    console_print("\n");
    
    // =========================================================================
    // ШАГ 1: Identity mapping первых 4GB (2MB страницами)
    // =========================================================================
    console_print("[PAGING] Step 1: Identity map 0-4GB...\n");
    
    for (uint64_t addr = 0; addr < 0x100000000ULL; addr += PAGE_SIZE_2M) {
        uint64_t flags = PAGE_WRITABLE;
        
        // =====================================================================
        // КРИТИЧЕСКОЕ ИСПРАВЛЕНИЕ: MMIO регионы должны быть Uncacheable!
        // =====================================================================
        // Local APIC: 0xFEE00000 (1 страница, но попадает в 2MB блок)
        // I/O APIC:   0xFEC00000
        // Обычно весь регион 0xFEC00000 - 0xFEEFFFFF — это MMIO
        if (addr >= 0xFEC00000ULL && addr < 0xFF000000ULL) {
            flags |= PAGE_NOCACHE | PAGE_WRITETHROUGH;
        }
        
        paging_map_2mb(pml4, addr, addr, flags);
    }
    
    console_print("[PAGING]   Done (APIC MMIO marked uncacheable)\n");
    
    // =========================================================================
    // ШАГ 2: Framebuffer (КРИТИЧНО!)
    // =========================================================================
    console_print("[PAGING] Step 2: Mapping framebuffer...\n");
    
    if (fb_addr >= 0x100000000ULL) {
        console_print("[PAGING]   Framebuffer above 4GB, mapping...\n");
        
        uint64_t fb_start = fb_addr & ~(PAGE_SIZE_2M - 1);
        uint64_t fb_end = (fb_addr + fb_size + PAGE_SIZE_2M - 1) & ~(PAGE_SIZE_2M - 1);
        
        for (uint64_t addr = fb_start; addr < fb_end; addr += PAGE_SIZE_2M) {
            paging_map_2mb(pml4, addr, addr, PAGE_WRITABLE | PAGE_NOCACHE);
        }
    } else {
        console_print("[PAGING]   Framebuffer in 0-4GB range (OK)\n");
    }
    
    // =========================================================================
    // ШАГ 3: Higher Half mapping для ядра
    // =========================================================================
    console_print("[PAGING] Step 3: Higher half kernel...\n");
    
    for (int i = 0; i < g_memory_map.count; i++) {
        uint32_t type = g_memory_map.regions[i].type;
        
        if (type == EfiLoaderCode || type == EfiLoaderData) {
            uint64_t phys_base = g_memory_map.regions[i].base;
            uint64_t pages = g_memory_map.regions[i].pages;
            uint64_t virt_base = KERNEL_VMA + phys_base;
            
            uint64_t flags = (type == EfiLoaderCode) ? 0 : PAGE_WRITABLE;
            
            for (uint64_t p = 0; p < pages; p++) {
                uint64_t phys = phys_base + (p * PAGE_SIZE_4K);
                uint64_t virt = virt_base + (p * PAGE_SIZE_4K);
                paging_map_page(pml4, virt, phys, flags);
            }
        }
    }
    
    console_print("[PAGING]   Done\n");
    
    // =========================================================================
    // ШАГ 4: Маппим ВСЮ физическую память
    // =========================================================================
    console_print("[PAGING] Step 4: Map all physical memory...\n");
    
    uint64_t max_phys = 0;
    for (int i = 0; i < g_memory_map.count; i++) {
        uint64_t end = g_memory_map.regions[i].base + 
                       g_memory_map.regions[i].pages * PAGE_SIZE_4K;
        if (end > max_phys) max_phys = end;
    }
    
    console_print("[PAGING]   Max physical: 0x");
    console_print_hex(max_phys);
    console_print("\n");
    
    if (max_phys > 0x100000000ULL) {
        for (uint64_t addr = 0x100000000ULL; addr < max_phys; addr += PAGE_SIZE_2M) {
            paging_map_2mb(pml4, addr, addr, PAGE_WRITABLE);
        }
        console_print("[PAGING]   Extended mapping done\n");
    }
    
    // =========================================================================
    // ШАГ 5: Финальная проверка
    // =========================================================================
    console_print("[PAGING] Step 5: Verification...\n");
    
    bool fb_mapped = paging_is_mapped(pml4, fb_addr);
    console_print("[PAGING]   Framebuffer mapped: ");
    console_print(fb_mapped ? "YES\n" : "NO!\n");
    
    if (!fb_mapped) {
        console_print("[PAGING] FATAL: Framebuffer not mapped!\n");
        return;
    }
    
    uint64_t current_rip;
    __asm__ volatile("lea (%%rip), %0" : "=r"(current_rip));
    bool code_mapped = paging_is_mapped(pml4, current_rip);
    console_print("[PAGING]   Current RIP (0x");
    console_print_hex(current_rip);
    console_print("): ");
    console_print(code_mapped ? "OK\n" : "NOT MAPPED!\n");
    
    if (!code_mapped) {
        console_print("[PAGING] FATAL: Code not mapped!\n");
        return;
    }
    
    // Проверяем маппинг APIC
    bool lapic_mapped = paging_is_mapped(pml4, 0xFEE00000ULL);
    bool ioapic_mapped = paging_is_mapped(pml4, 0xFEC00000ULL);
    console_print("[PAGING]   Local APIC (0xFEE00000): ");
    console_print(lapic_mapped ? "OK" : "NOT MAPPED!");
    console_print("\n");
    console_print("[PAGING]   I/O APIC (0xFEC00000): ");
    console_print(ioapic_mapped ? "OK" : "NOT MAPPED!");
    console_print("\n");
    
    // =========================================================================
    // ШАГ 6: Переключение CR3
    // =========================================================================
    console_print("[PAGING] Step 6: Activating...\n");
    console_print("[PAGING] Tables: ");
    console_print_dec(g_kernel_paging.tables_allocated);
    console_print("\n");
    
    paging_load_cr3((uint64_t)pml4);
    
    console_print("[PAGING] SUCCESS!\n");
    console_print("[PAGING] CR3: 0x");
    console_print_hex(paging_read_cr3());
    console_print("\n");
}

// ============================================================================
// Отладка
// ============================================================================

void paging_dump_tables(uint64_t *pml4, int max_entries) {
    if (!pml4) pml4 = g_kernel_paging.pml4;
    if (!pml4) return;
    
    console_print("\n=== PML4 Dump ===\n");
    
    int count = 0;
    for (int i = 0; i < 512 && count < max_entries; i++) {
        if (is_present(pml4[i])) {
            console_print("[");
            console_print_dec(i);
            console_print("] 0x");
            console_print_hex(pml4[i]);
            if (is_large_page(pml4[i])) console_print(" (LARGE)");
            console_print("\n");
            count++;
        }
    }
}

void paging_print_stats(void) {
    console_print("\n=== Paging Stats ===\n");
    console_print("PML4: 0x");
    console_print_hex((uint64_t)g_kernel_paging.pml4);
    console_print("\nTables: ");
    console_print_dec(g_kernel_paging.tables_allocated);
    console_print("\nMapped: ");
    console_print_dec(g_kernel_paging.total_mapped / 1024 / 1024);
    console_print(" MB\n");
}