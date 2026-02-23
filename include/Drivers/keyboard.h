#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>
#include <stdbool.h>

#include "Tables/idt.h"

// Специальные клавиши
typedef enum {
    KEY_NONE = 0,
    KEY_ESCAPE = 0x01,
    KEY_BACKSPACE = 0x0E,
    KEY_TAB = 0x0F,
    KEY_ENTER = 0x1C,
    KEY_LCTRL = 0x1D,
    KEY_LSHIFT = 0x2A,
    KEY_RSHIFT = 0x36,
    KEY_LALT = 0x38,
    KEY_CAPSLOCK = 0x3A,
    KEY_F1 = 0x3B,
    KEY_F2 = 0x3C,
    KEY_F3 = 0x3D,
    KEY_F4 = 0x3E,
    KEY_F5 = 0x3F,
    KEY_F6 = 0x40,
    KEY_F7 = 0x41,
    KEY_F8 = 0x42,
    KEY_F9 = 0x43,
    KEY_F10 = 0x44,
    KEY_F11 = 0x57,
    KEY_F12 = 0x58,
    KEY_NUMLOCK = 0x45,
    KEY_SCROLLLOCK = 0x46,
    KEY_HOME = 0x47,
    KEY_UP = 0x48,
    KEY_PAGEUP = 0x49,
    KEY_LEFT = 0x4B,
    KEY_RIGHT = 0x4D,
    KEY_END = 0x4F,
    KEY_DOWN = 0x50,
    KEY_PAGEDOWN = 0x51,
    KEY_INSERT = 0x52,
    KEY_DELETE = 0x53
} SpecialKey;

typedef struct {
    uint8_t scancode;       // Оригинальный скан-код
    char ascii;             // ASCII символ (0 если спец. клавиша)
    bool pressed;           // true = нажата, false = отпущена
    bool shift_held;        // Shift зажат
    bool ctrl_held;         // Ctrl зажат
    bool alt_held;          // Alt зажат
    bool capslock_on;       // CapsLock активен
} KeyboardKey;

// Состояние модификаторов
typedef struct {
    bool left_shift;
    bool right_shift;
    bool left_ctrl;
    bool right_ctrl;
    bool left_alt;
    bool right_alt;
    bool capslock;
    bool numlock;
    bool scrolllock;
} KeyboardState;

// Инициализация драйвера
void keyboard_driver_init(void);

// Обработчик прерывания
void keyboard_driver_handler(interrupt_frame_t* frame);

// Получить клавишу из буфера (NULL если буфер пуст)
KeyboardKey* try_get_key(void);

// Проверить, есть ли клавиши в буфере
bool keyboard_has_key(void);

// Получить текущее состояние модификаторов
KeyboardState* keyboard_get_state(void);

// Очистить буфер клавиатуры
void keyboard_clear_buffer(void);

#endif
