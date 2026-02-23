// include/io.h

#ifndef IO_H
#define IO_H

#include "stdint.h"

// Записать байт в порт
static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

// Прочитать байт из порта
static inline uint8_t inb(uint16_t port)
{
    uint8_t val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

// Записать слово (16 бит) в порт
static inline void outw(uint16_t port, uint16_t val)
{
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

// Прочитать слово (16 бит) из порта
static inline uint16_t inw(uint16_t port)
{
    uint16_t val;
    __asm__ volatile ("inw %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

// Записать двойное слово (32 бита) в порт
static inline void outl(uint16_t port, uint32_t val)
{
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}

// Прочитать двойное слово (32 бита) из порта
static inline uint32_t inl(uint16_t port)
{
    uint32_t val;
    __asm__ volatile ("inl %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

// Ожидание ввода-вывода (для старого железа)
// Запись в неиспользуемый порт 0x80 заставляет процессор ждать ~1-4 микросекунды
static inline void io_wait(void)
{
    outb(0x80, 0);
}

// Копирует count * 8 байт (count — количество 64-битных слов)
static inline void fast_memcpy64(void *dst, const void *src, uint64_t count)
{
    __asm__ volatile (
        "cld\n"
        "rep movsq\n"
        : "+D"(dst), "+S"(src), "+c"(count)
        : 
        : "memory"
    );
}

// Заполняет count * 8 байт значением val
static inline void fast_memset64(void *dst, uint64_t val, uint64_t count)
{
    __asm__ volatile (
        "cld\n"
        "rep stosq\n"
        : "+D"(dst), "+c"(count)
        : "a"(val)
        : "memory"
    );
}

#endif // IO_H