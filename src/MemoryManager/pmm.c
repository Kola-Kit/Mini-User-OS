#include <efi.h>
#include <efilib.h>
#include <stdio.h>

#include "MemoryManager/pmm.h"
#include "MemoryManager/uefiMapParser.h"

struct pmm g_pmm = {
    .free_list = NULL,
    .total_pages = 0,
    .free_pages = 0,
    .used_pages = 0
};

void pmm_init(void) {
    for (int i = 0; i < g_memory_map.count; i++) {
        g_pmm.total_pages += g_memory_map.regions[i].pages;
    }

    for (int i = 0; i < g_memory_map.count; i++) {
        if (g_memory_map.regions[i].type == EfiConventionalMemory) {
            uint64_t base = g_memory_map.regions[i].base;
            uint64_t pages = g_memory_map.regions[i].pages;

            for (uint64_t p = 0; p < pages; p++) {
                uint64_t page_addr = base + (p * 4096);

                if (page_addr == NULL_PAGE) {
                    g_pmm.used_pages++;
                    continue;
                }

                struct page_node *node = (struct page_node*)page_addr;

                node->next = g_pmm.free_list;
                g_pmm.free_list = node;

                g_pmm.free_pages++;
            }
        }
        else if (g_memory_map.regions[i].type == EfiLoaderCode ||
                g_memory_map.regions[i].type == EfiLoaderData) {
            
            // g_pmm.used_pages += g_memory_map.regions[i].pages;
        }
    }
}

void *pmm_alloc_page(void) {
    if (!g_pmm.free_list) { return NULL; }
    
    struct page_node *page = g_pmm.free_list;
    g_pmm.free_list = page->next;

    g_pmm.free_pages--;
    g_pmm.used_pages++;

    return (void*)page;
}

void pmm_free_page(void *page) {
    struct page_node *node = (struct page_node*)page;
    node->next = g_pmm.free_list;
    g_pmm.free_list = node;
    
    g_pmm.free_pages++;
    g_pmm.used_pages--;
}