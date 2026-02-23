// src/console.c

#include "stdint.h"
#include "Initialize/baseGraphic.h"
#include "Root/io.h"

// ============================================================================
// Состояние консоли
// ============================================================================

static uint32_t con_x     = 0;
static uint32_t con_y     = 0;
static uint32_t con_fg    = 0xFFFFFFFF;  // Белый
static uint32_t con_bg    = 0x00000000;  // Чёрный
static uint32_t con_tab   = 4;           // Ширина табуляции в символах

// Цвет фона для очистки строки
static uint32_t con_clear_bg = 0x00000000;

BOOLEAN avalible_output = TRUE;

// ============================================================================
// Внутренние функции
// ============================================================================

static uint32_t get_char_w(void)
{
    return CurrentFont.CharWidth  ? CurrentFont.CharWidth  : 8;
}

static uint32_t get_char_h(void)
{
    return CurrentFont.CharHeight ? CurrentFont.CharHeight : 16;
}

static uint32_t get_cols(void)
{
    return Graphics.Width / get_char_w();
}

static uint32_t get_rows(void)
{
    return Graphics.Height / get_char_h();
}

// Прокрутка экрана на одну строку вверх
static void scroll_up(void)
{
    // ДИАГНОСТИКА: Если буфера нет, будет тормозить.
    if (Graphics.BackBuffer == NULL) {
        // Раскомментируйте, если хотите проверить
        // PutPixel(0, 0, 0xFF0000); // Красная точка = нет буфера
    }

    uint32_t ch = get_char_h();
    // Используем буфер или видеопамять (если буфера нет)
    uint32_t *buffer = Graphics.BackBuffer ? Graphics.BackBuffer : Graphics.FramebufferBase;
    
    uint64_t screen_width = Graphics.Width;
    uint64_t total_pixels = Graphics.FramebufferSize / 4;
    
    // Сколько пикселей занимает одна строка текста (ширина * высота символа)
    uint64_t line_pixels = screen_width * ch;
    
    // Сколько пикселей нужно сдвинуть (весь экран минус верхняя строка текста)
    uint64_t copy_pixels = total_pixels - line_pixels;

    // 1. СДВИГ (Быстрое копирование в RAM)
    // Делим на 2, так как копируем по 8 байт (2 пикселя) за раз
    fast_memcpy64(buffer, buffer + line_pixels, copy_pixels / 2);

    // 2. ОЧИСТКА НИЗА
    uint32_t *bottom_ptr = buffer + copy_pixels;
    uint64_t bg64 = ((uint64_t)con_bg << 32) | con_bg;
    fast_memset64(bottom_ptr, bg64, line_pixels / 2);

    // 3. ОТПРАВКА НА ЭКРАН
    if (Graphics.BackBuffer) {
        // Копируем RAM -> VRAM
        // Это самая "тяжелая" часть, но rep movsq сделает её максимально быстро
        fast_memcpy64(Graphics.FramebufferBase, Graphics.BackBuffer, total_pixels / 2);
    }
}

// Перейти на следующую строку
static void newline(void)
{
    if (avalible_output) {
        con_x = 0;
        con_y++;

        if (con_y >= get_rows()) {
            scroll_up();
            con_y = get_rows() - 1;
        }
    }
}

// ============================================================================
// Публичные функции
// ============================================================================

void console_init(uint32_t fg, uint32_t bg)
{
    con_x  = 0;
    con_y  = 0;
    con_fg = fg;
    con_bg = bg;
    con_clear_bg = bg;
}

void console_set_color(uint32_t fg, uint32_t bg)
{
    if (avalible_output) {
        con_fg = fg;
        con_bg = bg;
    }
}

void console_clear(void)
{
    if (avalible_output) {
        FillScreen(con_bg);
        con_x = 0;
        con_y = 0;
    }
}

void console_putchar(char c)
{
    if (avalible_output) {
        uint32_t cw   = get_char_w();
        uint32_t ch   = get_char_h();
        uint32_t cols = get_cols();

        switch (c) {
            case '\n':
                newline();
                break;

            case '\r':
                con_x = 0;
                break;

            case '\t':
                con_x = (con_x + con_tab) & ~(con_tab - 1);
                if (con_x >= cols) newline();
                break;

            case '\b':
                if (con_x > 0) {
                    con_x--;
                    DrawChar(con_x * cw, con_y * ch, ' ', con_fg, con_bg);
                }
                break;

            default:
                if (c < 32) break;  // Игнорируем прочие управляющие символы

                DrawChar(con_x * cw, con_y * ch, c, con_fg, con_bg);
                con_x++;

                if (con_x >= cols) newline();
                break;
        }
    }
}

void console_print(const char *str)
{
    if (avalible_output) {
        if (!str) return;
        while (*str) {
            console_putchar(*str++);
        }
    }
}

// Вывод hex числа (64-bit)
void console_print_hex(uint64_t val)
{
    const char hex[] = "0123456789ABCDEF";
    char buf[19];  // "0x" + 16 цифр + '\0'
    buf[0]  = '0';
    buf[1]  = 'x';
    buf[18] = '\0';

    for (int i = 0; i < 16; i++) {
        buf[17 - i] = hex[val & 0xF];
        val >>= 4;
    }

    console_print(buf);
}

// Вывод десятичного числа (64-bit)
void console_print_dec(uint64_t val)
{
    if (val == 0) {
        console_putchar('0');
        return;
    }

    char buf[21];
    int  pos = 20;
    buf[pos] = '\0';

    while (val > 0) {
        buf[--pos] = '0' + (val % 10);
        val /= 10;
    }

    console_print(&buf[pos]);
}

// Вывод знакового десятичного числа
void console_print_int(int64_t val)
{
    if (val < 0) {
        console_putchar('-');
        console_print_dec((uint64_t)(-val));
    } else {
        console_print_dec((uint64_t)val);
    }
}

void enable_output(void) {
    avalible_output = TRUE;
}

void disable_output(void) {
    avalible_output = TRUE;
    // avalible_output = FALSE;
}