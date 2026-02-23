// src/console.c

#ifndef BASE_CONSOLE_H
#define BASE_CONSOLE_H

#include "stdint.h"
#include "Initialize/baseConsole.h"
#include "stdbool.h"

// ============================================================================
// Состояние консоли
// ============================================================================

uint32_t con_x     = 0;
uint32_t con_y     = 0;
uint32_t con_fg    = 0xFFFFFFFF;  // Белый
uint32_t con_bg    = 0x00000000;  // Чёрный
uint32_t con_tab   = 4;           // Ширина табуляции в символах

// Цвет фона для очистки строки
uint32_t con_clear_bg = 0x00000000;

// ============================================================================
// Внутренние функции
// ============================================================================

uint32_t get_char_w(void);

uint32_t get_char_h(void);

uint32_t get_cols(void);

uint32_t get_rows(void);

// Прокрутка экрана на одну строку вверх
void scroll_up(void);
// Перейти на следующую строку
void newline(void);
// ============================================================================
// Публичные функции
// ============================================================================

void console_init(uint32_t fg, uint32_t bg);
void console_set_color(uint32_t fg, uint32_t bg);

void console_clear(void);

void console_putchar(char c);

void console_print(const char *str);

// Вывод hex числа (64-bit)
void console_print_hex(uint64_t val);

// Вывод десятичного числа (64-bit)
void console_print_dec(uint64_t val);

// Вывод знакового десятичного числа
void console_print_int(int64_t val);

void enable_output(void);
void disable_output(void);

#endif
