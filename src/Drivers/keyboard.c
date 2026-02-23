#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include "Root/apic.h"
#include "Root/io.h"
#include "Tables/idt.h"
#include "Drivers/keyboard.h"

// Размер кольцевого буфера
#define KEYBOARD_BUFFER_SIZE 64

// Кольцевой буфер для клавиш
static KeyboardKey key_buffer[KEYBOARD_BUFFER_SIZE];
static volatile uint8_t buffer_head = 0;
static volatile uint8_t buffer_tail = 0;

// Текущее состояние модификаторов
static KeyboardState keyboard_state = {0};

// Временная переменная для возврата клавиши
static KeyboardKey current_key;

extern void console_print(const char *str);

// Таблица скан-кодов -> ASCII (без Shift)
static const char scancode_to_ascii[128] = {
    0,    0,   '1', '2', '3', '4', '5', '6',  // 0x00 - 0x07
    '7', '8', '9', '0', '-', '=',  0,   '\t', // 0x08 - 0x0F
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i',   // 0x10 - 0x17
    'o', 'p', '[', ']', '\n', 0,  'a', 's',   // 0x18 - 0x1F
    'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',   // 0x20 - 0x27
    '\'', '`', 0,  '\\', 'z', 'x', 'c', 'v',  // 0x28 - 0x2F
    'b', 'n', 'm', ',', '.', '/',  0,  '*',   // 0x30 - 0x37
    0,   ' ',  0,   0,   0,   0,   0,   0,    // 0x38 - 0x3F
    0,    0,   0,   0,   0,   0,   0,  '7',   // 0x40 - 0x47
    '8', '9', '-', '4', '5', '6', '+', '1',   // 0x48 - 0x4F
    '2', '3', '0', '.',  0,   0,   0,   0,    // 0x50 - 0x57
    0,    0,   0,   0,   0,   0,   0,   0,    // 0x58 - 0x5F
    0,    0,   0,   0,   0,   0,   0,   0,    // 0x60 - 0x67
    0,    0,   0,   0,   0,   0,   0,   0,    // 0x68 - 0x6F
    0,    0,   0,   0,   0,   0,   0,   0,    // 0x70 - 0x77
    0,    0,   0,   0,   0,   0,   0,   0     // 0x78 - 0x7F
};

// Таблица скан-кодов -> ASCII (с Shift)
static const char scancode_to_ascii_shift[128] = {
    0,    0,   '!', '@', '#', '$', '%', '^',  // 0x00 - 0x07
    '&', '*', '(', ')', '_', '+',  0,   '\t', // 0x08 - 0x0F
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I',   // 0x10 - 0x17
    'O', 'P', '{', '}', '\n', 0,  'A', 'S',   // 0x18 - 0x1F
    'D', 'F', 'G', 'H', 'J', 'K', 'L', ':',   // 0x20 - 0x27
    '"', '~',  0,  '|', 'Z', 'X', 'C', 'V',   // 0x28 - 0x2F
    'B', 'N', 'M', '<', '>', '?',  0,  '*',   // 0x30 - 0x37
    0,   ' ',  0,   0,   0,   0,   0,   0,    // 0x38 - 0x3F
    0,    0,   0,   0,   0,   0,   0,  '7',   // 0x40 - 0x47
    '8', '9', '-', '4', '5', '6', '+', '1',   // 0x48 - 0x4F
    '2', '3', '0', '.',  0,   0,   0,   0,    // 0x50 - 0x57
    0,    0,   0,   0,   0,   0,   0,   0,    // 0x58 - 0x5F
    0,    0,   0,   0,   0,   0,   0,   0,    // 0x60 - 0x67
    0,    0,   0,   0,   0,   0,   0,   0,    // 0x68 - 0x6F
    0,    0,   0,   0,   0,   0,   0,   0,    // 0x70 - 0x77
    0,    0,   0,   0,   0,   0,   0,   0     // 0x78 - 0x7F
};

// Проверка, является ли символ буквой
static bool is_letter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

// Преобразование к верхнему регистру
static char to_upper(char c) {
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 'A';
    }
    return c;
}

// Преобразование к нижнему регистру
static char to_lower(char c) {
    if (c >= 'A' && c <= 'Z') {
        return c - 'A' + 'a';
    }
    return c;
}

// Добавить клавишу в буфер
static void buffer_push(KeyboardKey* key) {
    uint8_t next_head = (buffer_head + 1) % KEYBOARD_BUFFER_SIZE;
    
    // Проверка на переполнение буфера
    if (next_head != buffer_tail) {
        key_buffer[buffer_head] = *key;
        buffer_head = next_head;
    }
    // Если буфер полон - клавиша теряется
}

// Извлечь клавишу из буфера
static bool buffer_pop(KeyboardKey* key) {
    if (buffer_head == buffer_tail) {
        return false; // Буфер пуст
    }
    
    *key = key_buffer[buffer_tail];
    buffer_tail = (buffer_tail + 1) % KEYBOARD_BUFFER_SIZE;
    return true;
}

// Обновить состояние модификаторов
static void update_modifiers(uint8_t scancode, bool pressed) {
    switch (scancode) {
        case KEY_LSHIFT:
            keyboard_state.left_shift = pressed;
            break;
        case KEY_RSHIFT:
            keyboard_state.right_shift = pressed;
            break;
        case KEY_LCTRL:
            keyboard_state.left_ctrl = pressed;
            break;
        case KEY_LALT:
            keyboard_state.left_alt = pressed;
            break;
        case KEY_CAPSLOCK:
            if (pressed) {
                keyboard_state.capslock = !keyboard_state.capslock;
            }
            break;
        case KEY_NUMLOCK:
            if (pressed) {
                keyboard_state.numlock = !keyboard_state.numlock;
            }
            break;
        case KEY_SCROLLLOCK:
            if (pressed) {
                keyboard_state.scrolllock = !keyboard_state.scrolllock;
            }
            break;
    }
}

// Проверить, зажат ли Shift
static bool is_shift_pressed(void) {
    return keyboard_state.left_shift || keyboard_state.right_shift;
}

// Проверить, зажат ли Ctrl
static bool is_ctrl_pressed(void) {
    return keyboard_state.left_ctrl || keyboard_state.right_ctrl;
}

// Проверить, зажат ли Alt
static bool is_alt_pressed(void) {
    return keyboard_state.left_alt || keyboard_state.right_alt;
}

// Получить ASCII символ для скан-кода
static char get_ascii(uint8_t scancode) {
    if (scancode >= 128) {
        return 0;
    }
    
    bool shift = is_shift_pressed();
    bool caps = keyboard_state.capslock;
    
    char c;
    if (shift) {
        c = scancode_to_ascii_shift[scancode];
    } else {
        c = scancode_to_ascii[scancode];
    }
    
    // CapsLock инвертирует регистр только для букв
    if (caps && is_letter(c)) {
        if (shift) {
            c = to_lower(c);
        } else {
            c = to_upper(c);
        }
    }
    
    return c;
}

// Инициализация драйвера клавиатуры
void keyboard_driver_init(void) {
    // Сброс состояния
    buffer_head = 0;
    buffer_tail = 0;
    
    keyboard_state.left_shift = false;
    keyboard_state.right_shift = false;
    keyboard_state.left_ctrl = false;
    keyboard_state.right_ctrl = false;
    keyboard_state.left_alt = false;
    keyboard_state.right_alt = false;
    keyboard_state.capslock = false;
    keyboard_state.numlock = false;
    keyboard_state.scrolllock = false;
    
    // Очистка буфера контроллера клавиатуры
    while (inb(0x64) & 0x01) {
        inb(0x60);
    }
    
    console_print("[KEYBOARD] Driver initialized\n");
}

// Обработчик прерывания клавиатуры
void keyboard_driver_handler(interrupt_frame_t* frame) {
    (void)frame;
    
    // Читаем скан-код (это сбрасывает IRQ линию)
    uint8_t scancode = inb(0x60);
    
    // Определяем, нажатие это или отпускание
    bool pressed = !(scancode & 0x80);
    uint8_t key_scancode = scancode & 0x7F;
    
    // Обновляем состояние модификаторов
    update_modifiers(key_scancode, pressed);
    
    // Создаём структуру клавиши
    KeyboardKey key;
    key.scancode = key_scancode;
    key.pressed = pressed;
    key.ascii = pressed ? get_ascii(key_scancode) : 0;
    key.shift_held = is_shift_pressed();
    key.ctrl_held = is_ctrl_pressed();
    key.alt_held = is_alt_pressed();
    key.capslock_on = keyboard_state.capslock;
    
    // Добавляем в буфер только нажатия (не отпускания)
    // Можно изменить, если нужны и события отпускания
    if (pressed) {
        buffer_push(&key);
    }
    
    lapic_send_eoi();
}

// Получить клавишу из буфера
KeyboardKey* try_get_key(void) {
    if (buffer_pop(&current_key)) {
        return &current_key;
    }
    return NULL;
}

// Проверить наличие клавиш в буфере
bool keyboard_has_key(void) {
    return buffer_head != buffer_tail;
}

// Получить текущее состояние модификаторов
KeyboardState* keyboard_get_state(void) {
    return &keyboard_state;
}

// Очистить буфер клавиатуры
void keyboard_clear_buffer(void) {
    buffer_head = 0;
    buffer_tail = 0;
}