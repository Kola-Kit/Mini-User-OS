#ifndef PMM_H
#define PMM_H

#define NULL_PAGE 0x0000

#include <stdint.h>

struct page_node {
    struct page_node *next;
};

struct pmm {
    struct page_node *free_list;
    uint64_t total_pages;
    uint64_t free_pages;
    uint64_t used_pages;
};

extern struct pmm g_pmm;

void pmm_init(void);
void *pmm_alloc_page(void);
void pmm_free_page(void *page);

#endif