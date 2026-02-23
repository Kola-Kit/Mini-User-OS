#ifndef HEAP_H
#define HEAP_H

#include <stdint.h>
#include <stddef.h>

#define HEAP_START 0xFFFF900000000000  // адрес кучи ядра
#define HEAP_INITIAL_SIZE 0x100000     // 1MB начальный размер

void heap_init(void);
void *kmalloc(size_t size);
void kfree(void *ptr);
void *kcalloc(size_t num, size_t size);
void *krealloc(void *ptr, size_t new_size);
void heap_print_stats(void);

#endif
