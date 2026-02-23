#include <stdio.h>
#include <string.h>

#include "MemoryManager/vmm.h"
#include "MemoryManager/pmm.h"
#include "MemoryManager/paging.h"

/* ==================== Глобальные переменные ==================== */

struct address_space *kernel_address_space = NULL;
static struct address_space *address_spaces = NULL;
static struct vm_region *kernel_regions = NULL;

extern void console_print(const char *str);

/* ==================== Вспомогательные функции ==================== */

/**
 * Выровнять адрес вверх до границы страницы
 */
static inline uint64_t align_up(uint64_t addr, uint64_t align) {
    return (addr + align - 1) & ~(align - 1);
}

/**
 * Выровнять адрес вниз до границы страницы
 */
static inline uint64_t align_down(uint64_t addr, uint64_t align) {
    return addr & ~(align - 1);
}

/**
 * Преобразовать виртуальный адрес в физический через таблицы страниц
 */
uint64_t virt_to_phys(void *pml4, uint64_t virt) {
    uint64_t *table = (uint64_t*)pml4;
    
    int indices[4] = {
        (virt >> 39) & 0x1FF,  /* PML4 index */
        (virt >> 30) & 0x1FF,  /* PDPT index */
        (virt >> 21) & 0x1FF,  /* PD index */
        (virt >> 12) & 0x1FF   /* PT index */
    };
    
    for (int level = 0; level < 4; level++) {
        if (!(table[indices[level]] & PAGE_PRESENT)) {
            return 0;
        }
        
        /* Проверка huge page на уровне PDPT (1GB) или PD (2MB) */
        if ((level == 1 || level == 2) && (table[indices[level]] & PAGE_HUGE)) {
            uint64_t base = table[indices[level]] & ~0xFFFULL;
            if (level == 1) {
                /* 1GB page */
                return base | (virt & 0x3FFFFFFFULL);
            } else {
                /* 2MB page */
                return base | (virt & 0x1FFFFFULL);
            }
        }
        
        table = (uint64_t*)(table[indices[level]] & ~0xFFFULL);
    }
    
    /* Обычная 4KB страница */
    return ((uint64_t)table & ~0xFFFULL) | (virt & 0xFFFULL);
}

/**
 * Освободить уровень таблицы страниц рекурсивно
 */
static void free_page_table_level(uint64_t *table, int level, int user_only) {
    for (int i = 0; i < 512; i++) {
        if (!(table[i] & PAGE_PRESENT)) continue;
        
        /* Пропускаем ядерную часть если user_only */
        if (user_only && level == 4 && i >= 256) continue;
        
        uint64_t *next_table = (uint64_t*)(table[i] & ~0xFFFULL);
        
        /* Пропускаем huge pages - они не содержат таблиц нижнего уровня */
        if (table[i] & PAGE_HUGE) {
            pmm_free_page(next_table);
            table[i] = 0;
            continue;
        }
        
        if (level > 1) {
            /* Рекурсивно обходим следующий уровень */
            free_page_table_level(next_table, level - 1, user_only);
            
            /* После рекурсии освобождаем саму таблицу */
            pmm_free_page(next_table);
            table[i] = 0;
        } else {
            /* level == 1 — это Page Table (PT) */
            uint64_t *pt = next_table;
            
            /* Освобождаем все физические страницы */
            for (int j = 0; j < 512; j++) {
                if (pt[j] & PAGE_PRESENT) {
                    uint64_t phys = pt[j] & ~0xFFFULL;
                    pmm_free_page((void*)phys);
                    pt[j] = 0;
                }
            }
            
            /* Освобождаем саму PT */
            pmm_free_page(pt);
            table[i] = 0;
        }
    }
}

/**
 * Освободить список регионов
 */
static void free_region_list(struct vm_region *regions) {
    struct vm_region *r = regions;
    while (r) {
        struct vm_region *next = r->next;
        pmm_free_page(r);
        r = next;
    }
}

/**
 * Инициализировать список регионов для адресного пространства
 */
static void vmm_init_address_space(struct address_space *as) {
    struct vm_region *initial = (struct vm_region*)pmm_alloc_page();
    if (!initial) return;
    
    memset(initial, 0, 4096);
    
    initial->start = USER_SPACE_START;
    initial->end = USER_SPACE_END;
    initial->flags = REGION_FREE;
    initial->type = 0;
    initial->next = NULL;
    initial->prev = NULL;
    
    as->regions = initial;
    as->mmap_base = USER_SPACE_END - 0x10000000ULL; /* Резерв для mmap */
}

/* ==================== Инициализация ==================== */

static void init_virtual_memory_manager(void) {
    kernel_regions = (struct vm_region*)pmm_alloc_page();
    if (!kernel_regions) return;
    
    memset(kernel_regions, 0, 4096);
    
    kernel_regions->start = KERNEL_HEAP_START;
    kernel_regions->end = KERNEL_HEAP_END;
    kernel_regions->flags = REGION_FREE;
    kernel_regions->next = NULL;
    kernel_regions->prev = NULL;
}

void vmm_init(void) {
    kernel_address_space = (struct address_space*)pmm_alloc_page();
    if (!kernel_address_space) return;
    
    memset(kernel_address_space, 0, 4096);
    
    kernel_address_space->pml4 = g_kernel_paging.pml4;
    kernel_address_space->heap_start = KERNEL_HEAP_START;
    kernel_address_space->heap_end = KERNEL_HEAP_START;
    kernel_address_space->stack_start = 0;
    kernel_address_space->stack_end = 0;
    kernel_address_space->ref_count = 1;
    kernel_address_space->next = NULL;
    
    init_virtual_memory_manager();
    
    console_print("[VMM] Virtual memory manager initialized\n");

    console_print("[VMM] Kernel address space at");
    console_print((char*)kernel_address_space);
    console_print("\n");
}

/* ==================== Управление адресными пространствами ==================== */

struct address_space *vmm_create_address_space(void) {
    struct address_space *as = (struct address_space*)pmm_alloc_page();
    if (!as) return NULL;
    
    memset(as, 0, 4096);
    
    /* Выделяем новую PML4 */
    uint64_t *new_pml4 = (uint64_t*)pmm_alloc_page();
    if (!new_pml4) {
        pmm_free_page(as);
        return NULL;
    }
    
    memset(new_pml4, 0, 4096);
    
    /* Копируем ядерную часть из PML4 ядра */
    uint64_t *kernel_pml4 = (uint64_t*)kernel_address_space->pml4;
    for (int i = 256; i < 512; i++) {
        new_pml4[i] = kernel_pml4[i];
    }
    
    as->pml4 = (void*)new_pml4;
    as->heap_start = USER_SPACE_START;
    as->heap_end = USER_SPACE_START;
    as->stack_start = 0;
    as->stack_end = 0;
    as->mmap_base = USER_SPACE_END - 0x10000000ULL;
    as->ref_count = 1;
    
    vmm_init_address_space(as);
    
    /* Добавляем в список адресных пространств */
    as->next = address_spaces;
    address_spaces = as;
    
    return as;
}

struct address_space *vmm_clone_address_space(struct address_space *src) {
    if (!src) return NULL;
    
    struct address_space *dst = vmm_create_address_space();
    if (!dst) return NULL;
    
    uint64_t *src_pml4 = (uint64_t*)src->pml4;
    uint64_t *dst_pml4 = (uint64_t*)dst->pml4;
    
    /* Копируем пользовательскую часть с COW (Copy-On-Write) */
    /* Упрощённая версия - полное копирование */
    for (int i = 0; i < 256; i++) {
        if (!(src_pml4[i] & PAGE_PRESENT)) continue;
        
        /* Для простоты копируем все страницы */
        /* TODO: Реализовать COW */
        uint64_t *src_pdpt = (uint64_t*)(src_pml4[i] & ~0xFFFULL);
        uint64_t *dst_pdpt = (uint64_t*)pmm_alloc_page();
        if (!dst_pdpt) {
            vmm_destroy_address_space(dst);
            return NULL;
        }
        
        memcpy(dst_pdpt, src_pdpt, 4096);
        dst_pml4[i] = (uint64_t)dst_pdpt | (src_pml4[i] & 0xFFFULL);
    }
    
    /* Копируем метаданные */
    dst->heap_start = src->heap_start;
    dst->heap_end = src->heap_end;
    dst->stack_start = src->stack_start;
    dst->stack_end = src->stack_end;
    dst->mmap_base = src->mmap_base;
    
    return dst;
}

void vmm_destroy_address_space(struct address_space *as) {
    if (!as || as == kernel_address_space) return;
    
    as->ref_count--;
    if (as->ref_count > 0) return;
    
    /* Освобождаем пользовательские таблицы страниц */
    uint64_t *pml = (uint64_t*)as->pml4;
    free_page_table_level(pml, 4, 1);
    
    /* Освобождаем PML4 */
    pmm_free_page(pml);
    
    /* Освобождаем список регионов */
    free_region_list(as->regions);
    
    /* Удаляем из списка адресных пространств */
    struct address_space **pp = &address_spaces;
    while (*pp) {
        if (*pp == as) {
            *pp = as->next;
            break;
        }
        pp = &(*pp)->next;
    }
    
    /* Освобождаем структуру */
    pmm_free_page(as);
}

void vmm_switch(struct address_space *as) {
    if (as && as->pml4) {
        paging_load_cr3((uint64_t)as->pml4);
    }
}

/* ==================== Управление регионами ==================== */

uint64_t vmm_find_free_region(struct address_space *as, size_t size) {
    return vmm_find_free_region_aligned(as, size, 4096);
}

uint64_t vmm_find_free_region_aligned(struct address_space *as, 
                                       size_t size, size_t alignment) {
    if (!as || size == 0) return 0;
    
    size_t needed = align_up(size, 4096);
    
    struct vm_region *r = as->regions;
    while (r) {
        if (r->flags == REGION_FREE) {
            uint64_t aligned_start = align_up(r->start, alignment);
            
            if (aligned_start + needed <= r->end) {
                return aligned_start;
            }
        }
        r = r->next;
    }
    
    return 0;
}

struct vm_region *vmm_find_region(struct address_space *as, uint64_t addr) {
    if (!as) return NULL;
    
    struct vm_region *r = as->regions;
    while (r) {
        if (addr >= r->start && addr < r->end) {
            return r;
        }
        r = r->next;
    }
    
    return NULL;
}

struct vm_region *vmm_create_region(struct address_space *as,
                                     uint64_t start, uint64_t end,
                                     uint32_t flags) {
    if (!as || start >= end) return NULL;
    
    struct vm_region *new_region = (struct vm_region*)pmm_alloc_page();
    if (!new_region) return NULL;
    
    memset(new_region, 0, sizeof(struct vm_region));
    
    new_region->start = start;
    new_region->end = end;
    new_region->flags = flags;
    
    /* Вставляем в отсортированный список */
    struct vm_region *prev = NULL;
    struct vm_region *curr = as->regions;
    
    while (curr && curr->start < start) {
        prev = curr;
        curr = curr->next;
    }
    
    new_region->next = curr;
    new_region->prev = prev;
    
    if (prev) {
        prev->next = new_region;
    } else {
        as->regions = new_region;
    }
    
    if (curr) {
        curr->prev = new_region;
    }
    
    return new_region;
}

/* ==================== Выделение и освобождение памяти ==================== */

void *vmm_alloc_pages(struct address_space *as, size_t count) {
    return vmm_alloc_pages_at(as, 0, count, 
                              REGION_USED | REGION_READ | REGION_WRITE);
}

void *vmm_alloc_pages_at(struct address_space *as, uint64_t virt_addr, 
                         size_t count, uint32_t flags) {
    if (!as || count == 0) return NULL;
    
    size_t pages = (count + 4095) / 4096;
    size_t needed = pages * 4096;
    
    /* Находим или используем указанный адрес */
    uint64_t virt;
    if (virt_addr == 0) {
        virt = vmm_find_free_region(as, needed);
        if (virt == 0) return NULL;
    } else {
        virt = align_down(virt_addr, 4096);
        
        /* Проверяем, что регион свободен */
        struct vm_region *r = vmm_find_region(as, virt);
        if (r && !(r->flags & REGION_FREE)) {
            return NULL;
        }
    }
    
    uint64_t first_virt = virt;
    
    /* Формируем флаги страниц */
    uint64_t page_flags = PAGE_PRESENT;
    if (flags & REGION_WRITE) page_flags |= PAGE_WRITABLE;
    if (flags & REGION_USER)  page_flags |= PAGE_USER;
    if (!(flags & REGION_EXEC)) page_flags |= PAGE_NO_EXECUTE;
    
    /* Выделяем и отображаем страницы */
    for (size_t i = 0; i < pages; i++) {
        void *phys = pmm_alloc_page();
        if (!phys) {
            /* Откат при ошибке */
            for (size_t j = 0; j < i; j++) {
                uint64_t addr = first_virt + j * 4096;
                uint64_t p = virt_to_phys(as->pml4, addr);
                paging_unmap_page(as->pml4, addr);
                if (p) pmm_free_page((void*)p);
            }
            return NULL;
        }
        
        /* Очищаем страницу */
        memset(phys, 0, 4096);
        
        paging_map_page(as->pml4, virt + i * 4096, (uint64_t)phys, page_flags);
    }
    
    /* Обновляем список регионов */
    struct vm_region *r = as->regions;
    while (r) {
        if ((r->flags == REGION_FREE) && r->start <= virt && virt < r->end) {
            uint64_t region_end = virt + needed;
            
            if (r->start == virt && region_end == r->end) {
                /* Занимаем весь регион */
                r->flags = flags;
            } else if (r->start == virt) {
                /* Занимаем начало региона */
                struct vm_region *new_r = vmm_create_region(as, virt, region_end, flags);
                if (new_r) {
                    r->start = region_end;
                }
            } else if (region_end == r->end) {
                /* Занимаем конец региона */
                r->end = virt;
                vmm_create_region(as, virt, region_end, flags);
            } else {
                /* Разбиваем регион на три части */
                uint64_t old_end = r->end;
                r->end = virt;
                vmm_create_region(as, virt, region_end, flags);
                vmm_create_region(as, region_end, old_end, REGION_FREE);
            }
            break;
        }
        r = r->next;
    }
    
    return (void*)first_virt;
}

void vmm_free_pages(struct address_space *as, void *addr, size_t count) {
    if (!as || !addr || count == 0) return;
    
    uint64_t virt = (uint64_t)addr;
    size_t pages = (count + 4095) / 4096;
    
    for (size_t i = 0; i < pages; i++) {
        uint64_t current_virt = virt + i * 4096;
        uint64_t phys = virt_to_phys(as->pml4, current_virt);
        
        if (phys) {
            paging_unmap_page(as->pml4, current_virt);
            pmm_free_page((void*)(phys & ~0xFFFULL));
        }
    }
    
    /* Помечаем регион как свободный и объединяем со соседями */
    struct vm_region *r = vmm_find_region(as, virt);
    if (r) {
        r->flags = REGION_FREE;
        
        /* Объединяем с предыдущим свободным регионом */
        if (r->prev && r->prev->flags == REGION_FREE) {
            struct vm_region *prev = r->prev;
            prev->end = r->end;
            prev->next = r->next;
            if (r->next) r->next->prev = prev;
            pmm_free_page(r);
            r = prev;
        }
        
        /* Объединяем со следующим свободным регионом */
        if (r->next && r->next->flags == REGION_FREE) {
            struct vm_region *next = r->next;
            r->end = next->end;
            r->next = next->next;
            if (next->next) next->next->prev = r;
            pmm_free_page(next);
        }
    }
}

/* ==================== Управление кучей ==================== */

int vmm_brk(struct address_space *as, uint64_t new_end) {
    if (!as) return -1;
    
    new_end = align_up(new_end, 4096);
    
    if (new_end < as->heap_start) return -1;
    
    if (new_end > as->heap_end) {
        /* Расширяем кучу */
        size_t pages_needed = (new_end - as->heap_end) / 4096;
        
        for (size_t i = 0; i < pages_needed; i++) {
            void *phys = pmm_alloc_page();
            if (!phys) return -1;
            
            memset(phys, 0, 4096);
            
            uint64_t virt = as->heap_end + i * 4096;
            paging_map_page(as->pml4, virt, (uint64_t)phys,
                           PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
        }
    } else if (new_end < as->heap_end) {
        /* Сжимаем кучу */
        size_t pages_to_free = (as->heap_end - new_end) / 4096;
        
        for (size_t i = 0; i < pages_to_free; i++) {
            uint64_t virt = as->heap_end - (i + 1) * 4096;
            uint64_t phys = virt_to_phys(as->pml4, virt);
            
            if (phys) {
                paging_unmap_page(as->pml4, virt);
                pmm_free_page((void*)phys);
            }
        }
    }
    
    as->heap_end = new_end;
    return 0;
}

void *vmm_sbrk(struct address_space *as, int64_t increment) {
    if (!as) return NULL;
    
    uint64_t old_end = as->heap_end;
    uint64_t new_end = old_end + increment;
    
    if (vmm_brk(as, new_end) < 0) {
        return NULL;
    }
    
    return (void*)old_end;
}

/* ==================== Управление стеком ==================== */

int vmm_setup_stack(struct address_space *as, uint64_t stack_top, size_t stack_size) {
    if (!as) return -1;
    
    stack_top = align_down(stack_top, 4096);
    stack_size = align_up(stack_size, 4096);
    
    uint64_t stack_bottom = stack_top - stack_size;
    
    /* Выделяем страницы для стека */
    size_t pages = stack_size / 4096;
    
    for (size_t i = 0; i < pages; i++) {
        void *phys = pmm_alloc_page();
        if (!phys) {
            /* Откат */
            for (size_t j = 0; j < i; j++) {
                uint64_t virt = stack_bottom + j * 4096;
                uint64_t p = virt_to_phys(as->pml4, virt);
                paging_unmap_page(as->pml4, virt);
                if (p) pmm_free_page((void*)p);
            }
            return -1;
        }
        
        memset(phys, 0, 4096);
        
        uint64_t virt = stack_bottom + i * 4096;
        paging_map_page(as->pml4, virt, (uint64_t)phys,
                       PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER | PAGE_NO_EXECUTE);
    }
    
    as->stack_start = stack_bottom;
    as->stack_end = stack_top;
    
    /* Создаём регион для стека */
    vmm_create_region(as, stack_bottom, stack_top, 
                      REGION_USED | REGION_STACK | REGION_READ | REGION_WRITE);
    
    return 0;
}

int vmm_expand_stack(struct address_space *as, uint64_t fault_addr) {
    if (!as || as->stack_start == 0) return -1;
    
    /* Проверяем, что адрес находится ниже текущего стека */
    if (fault_addr >= as->stack_start) return -1;
    
    /* Ограничиваем максимальный размер стека (например, 8 MB) */
    uint64_t max_stack_size = 8 * 1024 * 1024;
    if (as->stack_end - fault_addr > max_stack_size) return -1;
    
    /* Выделяем новую страницу */
    uint64_t new_page = align_down(fault_addr, 4096);
    void *phys = pmm_alloc_page();
    if (!phys) return -1;
    
    memset(phys, 0, 4096);
    
    paging_map_page(as->pml4, new_page, (uint64_t)phys,
                   PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER | PAGE_NO_EXECUTE);
    
    /* Обновляем границу стека */
    if (new_page < as->stack_start) {
        as->stack_start = new_page;
    }
    
    return 0;
}

/* ==================== Отображение памяти ==================== */

void *vmm_map_physical(struct address_space *as, uint64_t phys_addr,
                       size_t size, uint32_t flags) {
    if (!as || size == 0) return NULL;
    
    size = align_up(size, 4096);
    phys_addr = align_down(phys_addr, 4096);
    
    uint64_t virt = vmm_find_free_region(as, size);
    if (virt == 0) return NULL;
    
    uint64_t page_flags = PAGE_PRESENT;
    if (flags & REGION_WRITE) page_flags |= PAGE_WRITABLE;
    if (flags & REGION_USER)  page_flags |= PAGE_USER;
    
    size_t pages = size / 4096;
    for (size_t i = 0; i < pages; i++) {
        paging_map_page(as->pml4, virt + i * 4096, 
                       phys_addr + i * 4096, page_flags);
    }
    
    vmm_create_region(as, virt, virt + size, flags | REGION_MMAP);
    
    return (void*)virt;
}

void vmm_unmap(struct address_space *as, void *virt_addr, size_t size) {
    if (!as || !virt_addr || size == 0) return;
    
    uint64_t virt = align_down((uint64_t)virt_addr, 4096);
    size = align_up(size, 4096);
    
    size_t pages = size / 4096;
    for (size_t i = 0; i < pages; i++) {
        paging_unmap_page(as->pml4, virt + i * 4096);
    }
    
    /* Обновляем регионы */
    struct vm_region *r = vmm_find_region(as, virt);
    if (r) {
        r->flags = REGION_FREE;
    }
}

/* ==================== Защита памяти ==================== */

int vmm_protect(struct address_space *as, void *addr, size_t size, uint32_t flags) {
    if (!as || !addr || size == 0) return -1;
    
    uint64_t virt = align_down((uint64_t)addr, 4096);
    size = align_up(size, 4096);
    
    uint64_t page_flags = PAGE_PRESENT;
    if (flags & REGION_WRITE) page_flags |= PAGE_WRITABLE;
    if (flags & REGION_USER)  page_flags |= PAGE_USER;
    if (!(flags & REGION_EXEC)) page_flags |= PAGE_NO_EXECUTE;
    
    size_t pages = size / 4096;
    for (size_t i = 0; i < pages; i++) {
        uint64_t current_virt = virt + i * 4096;
        uint64_t phys = virt_to_phys(as->pml4, current_virt);
        
        if (phys) {
            /* Переотображаем с новыми флагами */
            paging_unmap_page(as->pml4, current_virt);
            paging_map_page(as->pml4, current_virt, phys, page_flags);
        }
    }
    
    /* Обновляем флаги региона */
    struct vm_region *r = vmm_find_region(as, virt);
    if (r) {
        r->flags = (r->flags & ~(REGION_READ | REGION_WRITE | REGION_EXEC)) | flags;
    }
    
    return 0;
}

/* ==================== Обработка page fault ==================== */

int vmm_handle_page_fault(struct address_space *as, uint64_t fault_addr,
                          uint32_t error_code) {
    if (!as) return -1;
    
    /* Проверяем, попадает ли адрес в область стека */
    if (as->stack_end > 0 && fault_addr < as->stack_start && 
        fault_addr >= as->stack_end - 8 * 1024 * 1024) {
        /* Попытка расширить стек */
        return vmm_expand_stack(as, fault_addr);
    }
    
    /* Проверяем, есть ли регион для этого адреса */
    struct vm_region *r = vmm_find_region(as, fault_addr);
    if (!r) return -1;
    
    /* Если регион свободен, это ошибка */
    if (r->flags == REGION_FREE) return -1;
    
    /* TODO: Обработка COW, demand paging и т.д. */
    
    return -1;
}

/* ==================== Вспомогательные функции ==================== */

uint64_t vmm_virt_to_phys(struct address_space *as, uint64_t virt) {
    if (!as) {
        as = kernel_address_space;
    }
    
    return virt_to_phys(as->pml4, virt);
}

int vmm_is_mapped(struct address_space *as, uint64_t virt) {
    return vmm_virt_to_phys(as, virt) != 0;
}

int vmm_check_access(struct address_space *as, uint64_t virt, uint32_t flags) {
    struct vm_region *r = vmm_find_region(as, virt);
    if (!r || r->flags == REGION_FREE) return 0;
    
    if ((flags & REGION_READ) && !(r->flags & REGION_READ)) return 0;
    if ((flags & REGION_WRITE) && !(r->flags & REGION_WRITE)) return 0;
    if ((flags & REGION_EXEC) && !(r->flags & REGION_EXEC)) return 0;
    
    return 1;
}

void vmm_get_stats(struct address_space *as, size_t *total_pages, size_t *used_pages) {
    if (!as) return;
    
    size_t total = 0;
    size_t used = 0;
    
    struct vm_region *r = as->regions;
    while (r) {
        size_t region_pages = (r->end - r->start) / 4096;
        total += region_pages;
        
        if (r->flags != REGION_FREE) {
            used += region_pages;
        }
        
        r = r->next;
    }
    
    if (total_pages) *total_pages = total;
    if (used_pages) *used_pages = used;
}

void vmm_dump(struct address_space *as) {
    if (!as) {
        console_print("[VMM] NULL address space\n");
        return;
    }
    
    console_print("[VMM] Address space dump:\n");

    console_print("  PML4: ");
    console_print((char*)as->pml4);
    console_print("\n");
    
    console_print("  Heap: 0x");
    console_print((char*)(unsigned long long)as->heap_start);
    console_print(" - 0x");
    console_print((char*)(unsigned long long)as->heap_end);
    console_print("\n");
    
    console_print("  Stack: 0x");
    console_print((char*)(unsigned long long)as->stack_start);
    console_print(" - 0x");
    console_print((char*)(unsigned long long)as->stack_end);
    console_print("\n");
    
    console_print("  Regions:\n");
    struct vm_region *r = as->regions;
    int count = 0;
    while (r && count < 20) {
        console_print("    [");
        console_print((char*)count);
        console_print("] 0x");
        console_print((char*)(unsigned long long)r->start);
        console_print(" - 0x");
        console_print((char*)(unsigned long long)r->end);
        console_print(" flags=0x");
        console_print((char*)r->flags);
        console_print("\n");
        
        r = r->next;
        count++;
    }
    
    if (r) {
        console_print("    ... (more regions)\n");
    }
}
