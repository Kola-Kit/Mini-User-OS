#ifndef VMM_H
#define VMM_H

#include <stdint.h>
#include <stddef.h>

/* Границы адресных пространств */
#define USER_SPACE_START    0x0000000000400000ULL  /* 4 MB */
#define USER_SPACE_END      0x00007FFFFFFFFFFFULL  /* Конец user space */
#define KERNEL_SPACE_START  0xFFFF800000000000ULL  /* Начало kernel space */
#define KERNEL_HEAP_START   0xFFFF800100000000ULL  /* Куча ядра */
#define KERNEL_HEAP_END     0xFFFF800200000000ULL  /* Конец кучи ядра */

/* Флаги регионов памяти */
#define REGION_FREE         0x00
#define REGION_USED         0x01
#define REGION_READ         0x02
#define REGION_WRITE        0x04
#define REGION_EXEC         0x08
#define REGION_USER         0x10
#define REGION_STACK        0x20
#define REGION_HEAP         0x40
#define REGION_MMAP         0x80

/* Флаги страниц (для совместимости с paging.h) */
#ifndef PAGE_PRESENT
#define PAGE_PRESENT        (1ULL << 0)
#define PAGE_WRITABLE       (1ULL << 1)
#define PAGE_USER           (1ULL << 2)
#define PAGE_WRITE_THROUGH  (1ULL << 3)
#define PAGE_CACHE_DISABLE  (1ULL << 4)
#define PAGE_ACCESSED       (1ULL << 5)
#define PAGE_DIRTY          (1ULL << 6)
#define PAGE_HUGE           (1ULL << 7)
#define PAGE_GLOBAL         (1ULL << 8)
#define PAGE_NO_EXECUTE     (1ULL << 63)
#endif

/* Регион виртуальной памяти */
struct vm_region {
    uint64_t start;           /* Начальный виртуальный адрес */
    uint64_t end;             /* Конечный виртуальный адрес */
    uint32_t flags;           /* Флаги региона */
    uint32_t type;            /* Тип региона (код, данные, стек и т.д.) */
    struct vm_region *next;   /* Следующий регион в списке */
    struct vm_region *prev;   /* Предыдущий регион в списке */
};

/* Адресное пространство процесса */
struct address_space {
    void *pml4;                   /* Указатель на PML4 таблицу */
    struct vm_region *regions;    /* Список регионов памяти */
    
    uint64_t heap_start;          /* Начало кучи */
    uint64_t heap_end;            /* Текущий конец кучи (brk) */
    
    uint64_t stack_start;         /* Начало стека (нижняя граница) */
    uint64_t stack_end;           /* Конец стека (верхняя граница) */
    
    uint64_t mmap_base;           /* База для mmap */
    
    uint32_t ref_count;           /* Счётчик ссылок */
    
    struct address_space *next;   /* Следующее адресное пространство */
};

/* Глобальное адресное пространство ядра */
extern struct address_space *kernel_address_space;

/* ==================== Инициализация ==================== */

/**
 * Инициализация менеджера виртуальной памяти
 * Вызывается один раз при загрузке ядра
 */
void vmm_init(void);

/* ==================== Управление адресными пространствами ==================== */

/**
 * Создать новое адресное пространство для процесса
 * @return Указатель на новое адресное пространство или NULL при ошибке
 */
struct address_space *vmm_create_address_space(void);

/**
 * Уничтожить адресное пространство и освободить все ресурсы
 * @param as Адресное пространство для уничтожения
 */
void vmm_destroy_address_space(struct address_space *as);

/**
 * Клонировать адресное пространство (для fork)
 * @param src Исходное адресное пространство
 * @return Копия адресного пространства или NULL при ошибке
 */
struct address_space *vmm_clone_address_space(struct address_space *src);

/**
 * Переключиться на адресное пространство
 * @param as Адресное пространство для активации
 */
void vmm_switch(struct address_space *as);

/* ==================== Выделение и освобождение памяти ==================== */

/**
 * Выделить страницы в адресном пространстве
 * @param as Адресное пространство
 * @param count Количество байт (будет округлено до страниц)
 * @return Виртуальный адрес выделенной памяти или NULL
 */
void *vmm_alloc_pages(struct address_space *as, size_t count);

/**
 * Выделить страницы по указанному виртуальному адресу
 * @param as Адресное пространство
 * @param virt_addr Желаемый виртуальный адрес
 * @param count Количество байт
 * @param flags Флаги страниц
 * @return Виртуальный адрес или NULL при ошибке
 */
void *vmm_alloc_pages_at(struct address_space *as, uint64_t virt_addr, 
                         size_t count, uint32_t flags);

/**
 * Освободить страницы
 * @param as Адресное пространство
 * @param addr Виртуальный адрес начала региона
 * @param count Количество байт
 */
void vmm_free_pages(struct address_space *as, void *addr, size_t count);

/* ==================== Управление регионами ==================== */

/**
 * Найти свободный регион заданного размера
 * @param as Адресное пространство
 * @param size Требуемый размер в байтах
 * @return Виртуальный адрес начала региона или 0
 */
uint64_t vmm_find_free_region(struct address_space *as, size_t size);

/**
 * Найти свободный регион с выравниванием
 * @param as Адресное пространство
 * @param size Требуемый размер
 * @param alignment Требуемое выравнивание
 * @return Виртуальный адрес или 0
 */
uint64_t vmm_find_free_region_aligned(struct address_space *as, 
                                       size_t size, size_t alignment);

/**
 * Найти регион по виртуальному адресу
 * @param as Адресное пространство
 * @param addr Виртуальный адрес
 * @return Указатель на регион или NULL
 */
struct vm_region *vmm_find_region(struct address_space *as, uint64_t addr);

/**
 * Создать новый регион памяти
 * @param as Адресное пространство
 * @param start Начальный адрес
 * @param end Конечный адрес
 * @param flags Флаги региона
 * @return Указатель на регион или NULL
 */
struct vm_region *vmm_create_region(struct address_space *as,
                                     uint64_t start, uint64_t end,
                                     uint32_t flags);

/* ==================== Управление кучей ==================== */

/**
 * Изменить размер кучи (аналог brk/sbrk)
 * @param as Адресное пространство
 * @param new_end Новый конец кучи
 * @return 0 при успехе, -1 при ошибке
 */
int vmm_brk(struct address_space *as, uint64_t new_end);

/**
 * Увеличить кучу на указанное количество байт
 * @param as Адресное пространство
 * @param increment Приращение в байтах
 * @return Предыдущий конец кучи или NULL при ошибке
 */
void *vmm_sbrk(struct address_space *as, int64_t increment);

/* ==================== Управление стеком ==================== */

/**
 * Настроить стек для процесса
 * @param as Адресное пространство
 * @param stack_top Вершина стека
 * @param stack_size Размер стека
 * @return 0 при успехе, -1 при ошибке
 */
int vmm_setup_stack(struct address_space *as, uint64_t stack_top, size_t stack_size);

/**
 * Расширить стек (при page fault)
 * @param as Адресное пространство
 * @param fault_addr Адрес, вызвавший page fault
 * @return 0 при успехе, -1 при ошибке
 */
int vmm_expand_stack(struct address_space *as, uint64_t fault_addr);

/* ==================== Отображение памяти ==================== */

/**
 * Отобразить физическую память в виртуальное пространство
 * @param as Адресное пространство
 * @param phys_addr Физический адрес
 * @param size Размер
 * @param flags Флаги
 * @return Виртуальный адрес или NULL
 */
void *vmm_map_physical(struct address_space *as, uint64_t phys_addr,
                       size_t size, uint32_t flags);

/**
 * Отменить отображение региона
 * @param as Адресное пространство
 * @param virt_addr Виртуальный адрес
 * @param size Размер
 */
void vmm_unmap(struct address_space *as, void *virt_addr, size_t size);

/* ==================== Защита памяти ==================== */

/**
 * Изменить права доступа к региону
 * @param as Адресное пространство
 * @param addr Виртуальный адрес
 * @param size Размер
 * @param flags Новые флаги
 * @return 0 при успехе, -1 при ошибке
 */
int vmm_protect(struct address_space *as, void *addr, size_t size, uint32_t flags);

/* ==================== Обработка page fault ==================== */

/**
 * Обработать page fault
 * @param as Адресное пространство
 * @param fault_addr Адрес, вызвавший fault
 * @param error_code Код ошибки от CPU
 * @return 0 если обработано, -1 если нужно завершить процесс
 */
int vmm_handle_page_fault(struct address_space *as, uint64_t fault_addr,
                          uint32_t error_code);

/* ==================== Вспомогательные функции ==================== */

/**
 * Преобразовать виртуальный адрес в физический
 * @param as Адресное пространство (или NULL для текущего)
 * @param virt Виртуальный адрес
 * @return Физический адрес или 0 при ошибке
 */
uint64_t vmm_virt_to_phys(struct address_space *as, uint64_t virt);

/**
 * Проверить, отображён ли виртуальный адрес
 * @param as Адресное пространство
 * @param virt Виртуальный адрес
 * @return 1 если отображён, 0 если нет
 */
int vmm_is_mapped(struct address_space *as, uint64_t virt);

/**
 * Проверить права доступа к адресу
 * @param as Адресное пространство
 * @param virt Виртуальный адрес
 * @param flags Требуемые права
 * @return 1 если доступ разрешён, 0 если нет
 */
int vmm_check_access(struct address_space *as, uint64_t virt, uint32_t flags);

/**
 * Получить статистику использования памяти
 * @param as Адресное пространство
 * @param total_pages Общее количество выделенных страниц
 * @param used_pages Количество используемых страниц
 */
void vmm_get_stats(struct address_space *as, size_t *total_pages, size_t *used_pages);

/**
 * Вывести отладочную информацию об адресном пространстве
 * @param as Адресное пространство
 */
void vmm_dump(struct address_space *as);

#endif /* VMM_H */