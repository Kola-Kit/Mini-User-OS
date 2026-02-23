#include "MemoryManager/heap.h"
#include "MemoryManager/pmm.h"
#include "MemoryManager/vmm.h"
#include "MemoryManager/paging.h"

// Блок кучи (заголовок)
typedef struct heap_block {
    size_t size;                // размер данных (без заголовка)
    uint8_t free;               // 1 = свободен, 0 = занят
    struct heap_block *next;     // следующий блок
    uint32_t magic;              // для проверки целостности (0xDEADBEEF)
} heap_block_t;

#define BLOCK_MAGIC 0xDEADBEEF
#define ALIGN(size) (((size) + 7) & ~7)  // выравнивание по 8 байт
#define BLOCK_SIZE sizeof(heap_block_t)

static heap_block_t *heap_first = NULL;
static uint64_t heap_current_size = 0;
static uint64_t heap_max_size = 0;

extern void console_print(const char *str);

// Инициализация кучи
void heap_init(void) {
    // Реально выделяем физическую память через VMM
    // (нужно pages = HEAP_INITIAL_SIZE / 4096 + 1)
    uint64_t pages = (HEAP_INITIAL_SIZE + 4095) / 4096;
    for (uint64_t i = 0; i < pages; i++) {
        void *phys = pmm_alloc_page();
        paging_map_page(g_kernel_paging.pml4, HEAP_START + i * 4096, (uint64_t)phys, 
                 PAGE_PRESENT | PAGE_WRITABLE);
    }

    // Выделяем начальную память для кучи через VMM
    heap_first = (heap_block_t*)HEAP_START;
    
    // Инициализируем первый блок
    heap_first->size = HEAP_INITIAL_SIZE - BLOCK_SIZE;
    heap_first->free = 1;
    heap_first->next = NULL;
    heap_first->magic = BLOCK_MAGIC;
    
    heap_current_size = HEAP_INITIAL_SIZE;
    heap_max_size = HEAP_INITIAL_SIZE;
    
    console_print("Heap initialized at 0x");
    console_print((char*)HEAP_START);
    console_print(", size ");
    console_print((char*)(HEAP_INITIAL_SIZE / 1024));
    console_print(" KB\n");
}

// Разделить блок, если он слишком большой
static void split_block(heap_block_t *block, size_t size) {
    if (block->size >= size + BLOCK_SIZE + 8) {
        heap_block_t *new_block = (heap_block_t*)((uint64_t)block + BLOCK_SIZE + size);
        new_block->size = block->size - size - BLOCK_SIZE;
        new_block->free = 1;
        new_block->next = block->next;
        new_block->magic = BLOCK_MAGIC;
        
        block->size = size;
        block->next = new_block;
    }
}

// Объединить соседние свободные блоки
static void merge_blocks(heap_block_t *block) {
    while (block->next && block->next->free) {
        heap_block_t *next = block->next;
        if (next->magic != BLOCK_MAGIC) {
            console_print("Heap corruption detected!\n");
            return;
        }
        block->size += BLOCK_SIZE + next->size;
        block->next = next->next;
    }
}

// Выделение памяти
void *kmalloc(size_t size) {
    if (size == 0) return NULL;
    
    size = ALIGN(size);  // выравнивание
    
    heap_block_t *current = heap_first;
    
    // Поиск подходящего свободного блока
    while (current) {
        if (current->magic != BLOCK_MAGIC) {
            console_print("Heap corruption at ");
            console_print((char*)current);
            console_print("!\n");
            return NULL;
        }
        
        if (current->free && current->size >= size) {
            split_block(current, size);
            current->free = 0;
            return (void*)((uint64_t)current + BLOCK_SIZE);
        }
        current = current->next;
    }
    
    // Нет свободного блока — нужно расширить кучу
    uint64_t expand_size = size + BLOCK_SIZE + 4096;  // + одна страница запаса
    uint64_t pages = (expand_size + 4095) / 4096;
    uint64_t new_size = pages * 4096;
    
    // Выделяем новые страницы через VMM
    for (uint64_t i = 0; i < pages; i++) {
        void *phys = pmm_alloc_page();
        paging_map_page(g_kernel_paging.pml4, HEAP_START + heap_current_size + i * 4096, 
                 (uint64_t)phys, PAGE_PRESENT | PAGE_WRITABLE);
    }
    
    // Создаём новый большой свободный блок в конце
    heap_block_t *new_block = (heap_block_t*)(HEAP_START + heap_current_size);
    new_block->size = new_size - BLOCK_SIZE;
    new_block->free = 1;
    new_block->next = NULL;
    new_block->magic = BLOCK_MAGIC;
    
    // Находим последний блок и добавляем новый
    current = heap_first;
    while (current->next) current = current->next;
    current->next = new_block;
    
    // Объединяем с предыдущим, если он свободен
    if (current->free) {
        merge_blocks(current);
    }
    
    heap_current_size += new_size;
    heap_max_size += new_size;
    
    // Пробуем выделить снова (рекурсивно, но один раз)
    return kmalloc(size);
}

// Освобождение памяти
void kfree(void *ptr) {
    if (!ptr) return;
    
    heap_block_t *block = (heap_block_t*)((uint64_t)ptr - BLOCK_SIZE);
    
    if (block->magic != BLOCK_MAGIC) {
        console_print("kfree: invalid pointer ");
        console_print((char*)ptr);
        console_print(" (bad magic)\n");
        return;
    }
    
    block->free = 1;
    
    // Объединяем с соседними свободными блоками
    heap_block_t *current = heap_first;
    while (current) {
        if (current->free) {
            merge_blocks(current);
        }
        current = current->next;
    }
}

// Выделение и обнуление памяти
void *kcalloc(size_t num, size_t size) {
    size_t total = num * size;
    void *ptr = kmalloc(total);
    if (ptr) {
        // Обнуляем память
        uint64_t *p = ptr;
        for (size_t i = 0; i < total / 8; i++) {
            p[i] = 0;
        }
    }
    return ptr;
}

// Изменение размера блока
void *krealloc(void *ptr, size_t new_size) {
    if (!ptr) return kmalloc(new_size);
    if (new_size == 0) {
        kfree(ptr);
        return NULL;
    }
    
    heap_block_t *block = (heap_block_t*)((uint64_t)ptr - BLOCK_SIZE);
    if (block->magic != BLOCK_MAGIC) {
        console_print("krealloc: invalid pointer\n");
        return NULL;
    }
    
    // Если текущий блок достаточно велик
    if (block->size >= new_size) {
        return ptr;
    }
    
    // Иначе выделяем новый блок и копируем данные
    void *new_ptr = kmalloc(new_size);
    if (new_ptr) {
        // Копируем старые данные
        uint64_t *src = ptr;
        uint64_t *dst = new_ptr;
        for (size_t i = 0; i < block->size / 8; i++) {
            dst[i] = src[i];
        }
        kfree(ptr);
    }
    return new_ptr;
}

// Печать статистики кучи
void heap_print_stats(void) {
    heap_block_t *current = heap_first;
    uint64_t total_free = 0;
    uint64_t total_used = 0;
    int blocks = 0;
    
    while (current) {
        if (current->magic != BLOCK_MAGIC) {
            console_print("Heap corrupted at block ");
            console_print((char*)current);
            console_print("\n");
            return;
        }
        
        if (current->free) {
            total_free += current->size;
        } else {
            total_used += current->size;
        }
        blocks++;
        current = current->next;
    }

    console_print("Heap stats: total ");
    console_print((char*)((int)heap_current_size / 1024));
    console_print(" KB, used ");
    console_print((char*)((int)total_used / 1024));
    console_print(" KB, free ");
    console_print((char*)((int)total_free / 1024));
    console_print(" KB, blocks ");
    console_print((char*)blocks);
    console_print("\n");
}