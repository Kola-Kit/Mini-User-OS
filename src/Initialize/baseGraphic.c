#include "Initialize/baseGraphic.h"
#include <stdarg.h>

// ============================================================================
// Глобальные переменные
// ============================================================================

EFI_GUID GopGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
GRAPHICS_INFO Graphics = {0};
EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop = NULL;
FONT_INFO CurrentFont = {0};

static void *fast_memcpy(void *dst, const void *src, uint64_t n) {
    uint64_t *d = (uint64_t *)dst;
    const uint64_t *s = (const uint64_t *)src;
    uint64_t i;
    for (i = 0; i < n / 8; i++) {
        d[i] = s[i];
    }
    // Хвосты можно докопировать байтами, но для графики обычно кратно 4/8
    return dst;
}

// ============================================================================
// Встроенный шрифт 8x16 (ASCII 32-126)
// Классический VGA/BIOS шрифт
// ============================================================================

static const UINT8 Font8x16_Data[] = {
    // Символ 32 (пробел)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // Символ 33 (!)
    0x00, 0x00, 0x18, 0x3C, 0x3C, 0x3C, 0x18, 0x18,
    0x18, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00,
    // Символ 34 (")
    0x00, 0x66, 0x66, 0x66, 0x24, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // Символ 35 (#)
    0x00, 0x00, 0x00, 0x6C, 0x6C, 0xFE, 0x6C, 0x6C,
    0x6C, 0xFE, 0x6C, 0x6C, 0x00, 0x00, 0x00, 0x00,
    // Символ 36 ($)
    0x18, 0x18, 0x7C, 0xC6, 0xC2, 0xC0, 0x7C, 0x06,
    0x06, 0x86, 0xC6, 0x7C, 0x18, 0x18, 0x00, 0x00,
    // Символ 37 (%)
    0x00, 0x00, 0x00, 0x00, 0xC2, 0xC6, 0x0C, 0x18,
    0x30, 0x60, 0xC6, 0x86, 0x00, 0x00, 0x00, 0x00,
    // Символ 38 (&)
    0x00, 0x00, 0x38, 0x6C, 0x6C, 0x38, 0x76, 0xDC,
    0xCC, 0xCC, 0xCC, 0x76, 0x00, 0x00, 0x00, 0x00,
    // Символ 39 (')
    0x00, 0x30, 0x30, 0x30, 0x60, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // Символ 40 (()
    0x00, 0x00, 0x0C, 0x18, 0x30, 0x30, 0x30, 0x30,
    0x30, 0x30, 0x18, 0x0C, 0x00, 0x00, 0x00, 0x00,
    // Символ 41 ())
    0x00, 0x00, 0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x0C,
    0x0C, 0x0C, 0x18, 0x30, 0x00, 0x00, 0x00, 0x00,
    // Символ 42 (*)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x66, 0x3C, 0xFF,
    0x3C, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // Символ 43 (+)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x7E,
    0x18, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // Символ 44 (,)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x18, 0x18, 0x18, 0x30, 0x00, 0x00, 0x00,
    // Символ 45 (-)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFE,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // Символ 46 (.)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00,
    // Символ 47 (/)
    0x00, 0x00, 0x00, 0x00, 0x02, 0x06, 0x0C, 0x18,
    0x30, 0x60, 0xC0, 0x80, 0x00, 0x00, 0x00, 0x00,
    // Символ 48 (0)
    0x00, 0x00, 0x38, 0x6C, 0xC6, 0xC6, 0xD6, 0xD6,
    0xC6, 0xC6, 0x6C, 0x38, 0x00, 0x00, 0x00, 0x00,
    // Символ 49 (1)
    0x00, 0x00, 0x18, 0x38, 0x78, 0x18, 0x18, 0x18,
    0x18, 0x18, 0x18, 0x7E, 0x00, 0x00, 0x00, 0x00,
    // Символ 50 (2)
    0x00, 0x00, 0x7C, 0xC6, 0x06, 0x0C, 0x18, 0x30,
    0x60, 0xC0, 0xC6, 0xFE, 0x00, 0x00, 0x00, 0x00,
    // Символ 51 (3)
    0x00, 0x00, 0x7C, 0xC6, 0x06, 0x06, 0x3C, 0x06,
    0x06, 0x06, 0xC6, 0x7C, 0x00, 0x00, 0x00, 0x00,
    // Символ 52 (4)
    0x00, 0x00, 0x0C, 0x1C, 0x3C, 0x6C, 0xCC, 0xFE,
    0x0C, 0x0C, 0x0C, 0x1E, 0x00, 0x00, 0x00, 0x00,
    // Символ 53 (5)
    0x00, 0x00, 0xFE, 0xC0, 0xC0, 0xC0, 0xFC, 0x06,
    0x06, 0x06, 0xC6, 0x7C, 0x00, 0x00, 0x00, 0x00,
    // Символ 54 (6)
    0x00, 0x00, 0x38, 0x60, 0xC0, 0xC0, 0xFC, 0xC6,
    0xC6, 0xC6, 0xC6, 0x7C, 0x00, 0x00, 0x00, 0x00,
    // Символ 55 (7)
    0x00, 0x00, 0xFE, 0xC6, 0x06, 0x06, 0x0C, 0x18,
    0x30, 0x30, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00,
    // Символ 56 (8)
    0x00, 0x00, 0x7C, 0xC6, 0xC6, 0xC6, 0x7C, 0xC6,
    0xC6, 0xC6, 0xC6, 0x7C, 0x00, 0x00, 0x00, 0x00,
    // Символ 57 (9)
    0x00, 0x00, 0x7C, 0xC6, 0xC6, 0xC6, 0x7E, 0x06,
    0x06, 0x06, 0x0C, 0x78, 0x00, 0x00, 0x00, 0x00,
    // Символ 58 (:)
    0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00, 0x00,
    0x00, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00,
    // Символ 59 (;)
    0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00, 0x00,
    0x00, 0x18, 0x18, 0x30, 0x00, 0x00, 0x00, 0x00,
    // Символ 60 (<)
    0x00, 0x00, 0x00, 0x06, 0x0C, 0x18, 0x30, 0x60,
    0x30, 0x18, 0x0C, 0x06, 0x00, 0x00, 0x00, 0x00,
    // Символ 61 (=)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x00, 0x00,
    0x7E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // Символ 62 (>)
    0x00, 0x00, 0x00, 0x60, 0x30, 0x18, 0x0C, 0x06,
    0x0C, 0x18, 0x30, 0x60, 0x00, 0x00, 0x00, 0x00,
    // Символ 63 (?)
    0x00, 0x00, 0x7C, 0xC6, 0xC6, 0x0C, 0x18, 0x18,
    0x18, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00,
    // Символ 64 (@)
    0x00, 0x00, 0x00, 0x7C, 0xC6, 0xC6, 0xDE, 0xDE,
    0xDE, 0xDC, 0xC0, 0x7C, 0x00, 0x00, 0x00, 0x00,
    // Символ 65 (A)
    0x00, 0x00, 0x10, 0x38, 0x6C, 0xC6, 0xC6, 0xFE,
    0xC6, 0xC6, 0xC6, 0xC6, 0x00, 0x00, 0x00, 0x00,
    // Символ 66 (B)
    0x00, 0x00, 0xFC, 0x66, 0x66, 0x66, 0x7C, 0x66,
    0x66, 0x66, 0x66, 0xFC, 0x00, 0x00, 0x00, 0x00,
    // Символ 67 (C)
    0x00, 0x00, 0x3C, 0x66, 0xC2, 0xC0, 0xC0, 0xC0,
    0xC0, 0xC2, 0x66, 0x3C, 0x00, 0x00, 0x00, 0x00,
    // Символ 68 (D)
    0x00, 0x00, 0xF8, 0x6C, 0x66, 0x66, 0x66, 0x66,
    0x66, 0x66, 0x6C, 0xF8, 0x00, 0x00, 0x00, 0x00,
    // Символ 69 (E)
    0x00, 0x00, 0xFE, 0x66, 0x62, 0x68, 0x78, 0x68,
    0x60, 0x62, 0x66, 0xFE, 0x00, 0x00, 0x00, 0x00,
    // Символ 70 (F)
    0x00, 0x00, 0xFE, 0x66, 0x62, 0x68, 0x78, 0x68,
    0x60, 0x60, 0x60, 0xF0, 0x00, 0x00, 0x00, 0x00,
    // Символ 71 (G)
    0x00, 0x00, 0x3C, 0x66, 0xC2, 0xC0, 0xC0, 0xDE,
    0xC6, 0xC6, 0x66, 0x3A, 0x00, 0x00, 0x00, 0x00,
    // Символ 72 (H)
    0x00, 0x00, 0xC6, 0xC6, 0xC6, 0xC6, 0xFE, 0xC6,
    0xC6, 0xC6, 0xC6, 0xC6, 0x00, 0x00, 0x00, 0x00,
    // Символ 73 (I)
    0x00, 0x00, 0x3C, 0x18, 0x18, 0x18, 0x18, 0x18,
    0x18, 0x18, 0x18, 0x3C, 0x00, 0x00, 0x00, 0x00,
    // Символ 74 (J)
    0x00, 0x00, 0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C,
    0xCC, 0xCC, 0xCC, 0x78, 0x00, 0x00, 0x00, 0x00,
    // Символ 75 (K)
    0x00, 0x00, 0xE6, 0x66, 0x66, 0x6C, 0x78, 0x78,
    0x6C, 0x66, 0x66, 0xE6, 0x00, 0x00, 0x00, 0x00,
    // Символ 76 (L)
    0x00, 0x00, 0xF0, 0x60, 0x60, 0x60, 0x60, 0x60,
    0x60, 0x62, 0x66, 0xFE, 0x00, 0x00, 0x00, 0x00,
    // Символ 77 (M)
    0x00, 0x00, 0xC6, 0xEE, 0xFE, 0xFE, 0xD6, 0xC6,
    0xC6, 0xC6, 0xC6, 0xC6, 0x00, 0x00, 0x00, 0x00,
    // Символ 78 (N)
    0x00, 0x00, 0xC6, 0xE6, 0xF6, 0xFE, 0xDE, 0xCE,
    0xC6, 0xC6, 0xC6, 0xC6, 0x00, 0x00, 0x00, 0x00,
    // Символ 79 (O)
    0x00, 0x00, 0x7C, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6,
    0xC6, 0xC6, 0xC6, 0x7C, 0x00, 0x00, 0x00, 0x00,
    // Символ 80 (P)
    0x00, 0x00, 0xFC, 0x66, 0x66, 0x66, 0x7C, 0x60,
    0x60, 0x60, 0x60, 0xF0, 0x00, 0x00, 0x00, 0x00,
    // Символ 81 (Q)
    0x00, 0x00, 0x7C, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6,
    0xC6, 0xD6, 0xDE, 0x7C, 0x0C, 0x0E, 0x00, 0x00,
    // Символ 82 (R)
    0x00, 0x00, 0xFC, 0x66, 0x66, 0x66, 0x7C, 0x6C,
    0x66, 0x66, 0x66, 0xE6, 0x00, 0x00, 0x00, 0x00,
    // Символ 83 (S)
    0x00, 0x00, 0x7C, 0xC6, 0xC6, 0x60, 0x38, 0x0C,
    0x06, 0xC6, 0xC6, 0x7C, 0x00, 0x00, 0x00, 0x00,
    // Символ 84 (T)
    0x00, 0x00, 0xFF, 0xDB, 0x99, 0x18, 0x18, 0x18,
    0x18, 0x18, 0x18, 0x3C, 0x00, 0x00, 0x00, 0x00,
    // Символ 85 (U)
    0x00, 0x00, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6,
    0xC6, 0xC6, 0xC6, 0x7C, 0x00, 0x00, 0x00, 0x00,
    // Символ 86 (V)
    0x00, 0x00, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6,
    0xC6, 0x6C, 0x38, 0x10, 0x00, 0x00, 0x00, 0x00,
    // Символ 87 (W)
    0x00, 0x00, 0xC6, 0xC6, 0xC6, 0xC6, 0xD6, 0xD6,
    0xD6, 0xFE, 0xEE, 0x6C, 0x00, 0x00, 0x00, 0x00,
    // Символ 88 (X)
    0x00, 0x00, 0xC6, 0xC6, 0x6C, 0x7C, 0x38, 0x38,
    0x7C, 0x6C, 0xC6, 0xC6, 0x00, 0x00, 0x00, 0x00,
    // Символ 89 (Y)
    0x00, 0x00, 0xC3, 0xC3, 0xC3, 0x66, 0x3C, 0x18,
    0x18, 0x18, 0x18, 0x3C, 0x00, 0x00, 0x00, 0x00,
    // Символ 90 (Z)
    0x00, 0x00, 0xFE, 0xC6, 0x86, 0x0C, 0x18, 0x30,
    0x60, 0xC2, 0xC6, 0xFE, 0x00, 0x00, 0x00, 0x00,
    // Символ 91 ([)
    0x00, 0x00, 0x3C, 0x30, 0x30, 0x30, 0x30, 0x30,
    0x30, 0x30, 0x30, 0x3C, 0x00, 0x00, 0x00, 0x00,
    // Символ 92 (\)
    0x00, 0x00, 0x00, 0x80, 0xC0, 0xE0, 0x70, 0x38,
    0x1C, 0x0E, 0x06, 0x02, 0x00, 0x00, 0x00, 0x00,
    // Символ 93 (])
    0x00, 0x00, 0x3C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C,
    0x0C, 0x0C, 0x0C, 0x3C, 0x00, 0x00, 0x00, 0x00,
    // Символ 94 (^)
    0x10, 0x38, 0x6C, 0xC6, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // Символ 95 (_)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00,
    // Символ 96 (`)
    0x30, 0x30, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // Символ 97 (a)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x78, 0x0C, 0x7C,
    0xCC, 0xCC, 0xCC, 0x76, 0x00, 0x00, 0x00, 0x00,
    // Символ 98 (b)
    0x00, 0x00, 0xE0, 0x60, 0x60, 0x78, 0x6C, 0x66,
    0x66, 0x66, 0x66, 0x7C, 0x00, 0x00, 0x00, 0x00,
    // Символ 99 (c)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x7C, 0xC6, 0xC0,
    0xC0, 0xC0, 0xC6, 0x7C, 0x00, 0x00, 0x00, 0x00,
    // Символ 100 (d)
    0x00, 0x00, 0x1C, 0x0C, 0x0C, 0x3C, 0x6C, 0xCC,
    0xCC, 0xCC, 0xCC, 0x76, 0x00, 0x00, 0x00, 0x00,
    // Символ 101 (e)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x7C, 0xC6, 0xFE,
    0xC0, 0xC0, 0xC6, 0x7C, 0x00, 0x00, 0x00, 0x00,
    // Символ 102 (f)
    0x00, 0x00, 0x38, 0x6C, 0x64, 0x60, 0xF0, 0x60,
    0x60, 0x60, 0x60, 0xF0, 0x00, 0x00, 0x00, 0x00,
    // Символ 103 (g)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x76, 0xCC, 0xCC,
    0xCC, 0xCC, 0xCC, 0x7C, 0x0C, 0xCC, 0x78, 0x00,
    // Символ 104 (h)
    0x00, 0x00, 0xE0, 0x60, 0x60, 0x6C, 0x76, 0x66,
    0x66, 0x66, 0x66, 0xE6, 0x00, 0x00, 0x00, 0x00,
    // Символ 105 (i)
    0x00, 0x00, 0x18, 0x18, 0x00, 0x38, 0x18, 0x18,
    0x18, 0x18, 0x18, 0x3C, 0x00, 0x00, 0x00, 0x00,
    // Символ 106 (j)
    0x00, 0x00, 0x06, 0x06, 0x00, 0x0E, 0x06, 0x06,
    0x06, 0x06, 0x06, 0x06, 0x66, 0x66, 0x3C, 0x00,
    // Символ 107 (k)
    0x00, 0x00, 0xE0, 0x60, 0x60, 0x66, 0x6C, 0x78,
    0x78, 0x6C, 0x66, 0xE6, 0x00, 0x00, 0x00, 0x00,
    // Символ 108 (l)
    0x00, 0x00, 0x38, 0x18, 0x18, 0x18, 0x18, 0x18,
    0x18, 0x18, 0x18, 0x3C, 0x00, 0x00, 0x00, 0x00,
    // Символ 109 (m)
    0x00, 0x00, 0x00, 0x00, 0x00, 0xE6, 0xFF, 0xDB,
    0xDB, 0xDB, 0xDB, 0xDB, 0x00, 0x00, 0x00, 0x00,
    // Символ 110 (n)
    0x00, 0x00, 0x00, 0x00, 0x00, 0xDC, 0x66, 0x66,
    0x66, 0x66, 0x66, 0x66, 0x00, 0x00, 0x00, 0x00,
    // Символ 111 (o)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x7C, 0xC6, 0xC6,
    0xC6, 0xC6, 0xC6, 0x7C, 0x00, 0x00, 0x00, 0x00,
    // Символ 112 (p)
    0x00, 0x00, 0x00, 0x00, 0x00, 0xDC, 0x66, 0x66,
    0x66, 0x66, 0x66, 0x7C, 0x60, 0x60, 0xF0, 0x00,
    // Символ 113 (q)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x76, 0xCC, 0xCC,
    0xCC, 0xCC, 0xCC, 0x7C, 0x0C, 0x0C, 0x1E, 0x00,
    // Символ 114 (r)
    0x00, 0x00, 0x00, 0x00, 0x00, 0xDC, 0x76, 0x66,
    0x60, 0x60, 0x60, 0xF0, 0x00, 0x00, 0x00, 0x00,
    // Символ 115 (s)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x7C, 0xC6, 0x60,
    0x38, 0x0C, 0xC6, 0x7C, 0x00, 0x00, 0x00, 0x00,
    // Символ 116 (t)
    0x00, 0x00, 0x10, 0x30, 0x30, 0xFC, 0x30, 0x30,
    0x30, 0x30, 0x36, 0x1C, 0x00, 0x00, 0x00, 0x00,
    // Символ 117 (u)
    0x00, 0x00, 0x00, 0x00, 0x00, 0xCC, 0xCC, 0xCC,
    0xCC, 0xCC, 0xCC, 0x76, 0x00, 0x00, 0x00, 0x00,
    // Символ 118 (v)
    0x00, 0x00, 0x00, 0x00, 0x00, 0xC3, 0xC3, 0xC3,
    0xC3, 0x66, 0x3C, 0x18, 0x00, 0x00, 0x00, 0x00,
    // Символ 119 (w)
    0x00, 0x00, 0x00, 0x00, 0x00, 0xC6, 0xC6, 0xC6,
    0xD6, 0xFE, 0xEE, 0x6C, 0x00, 0x00, 0x00, 0x00,
    // Символ 120 (x)
    0x00, 0x00, 0x00, 0x00, 0x00, 0xC6, 0x6C, 0x38,
    0x38, 0x38, 0x6C, 0xC6, 0x00, 0x00, 0x00, 0x00,
    // Символ 121 (y)
    0x00, 0x00, 0x00, 0x00, 0x00, 0xC6, 0xC6, 0xC6,
    0xC6, 0xC6, 0xC6, 0x7E, 0x06, 0x0C, 0xF8, 0x00,
    // Символ 122 (z)
    0x00, 0x00, 0x00, 0x00, 0x00, 0xFE, 0xCC, 0x18,
    0x30, 0x60, 0xC6, 0xFE, 0x00, 0x00, 0x00, 0x00,
    // Символ 123 ({)
    0x00, 0x00, 0x0E, 0x18, 0x18, 0x18, 0x70, 0x18,
    0x18, 0x18, 0x18, 0x0E, 0x00, 0x00, 0x00, 0x00,
    // Символ 124 (|)
    0x00, 0x00, 0x18, 0x18, 0x18, 0x18, 0x00, 0x18,
    0x18, 0x18, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00,
    // Символ 125 (})
    0x00, 0x00, 0x70, 0x18, 0x18, 0x18, 0x0E, 0x18,
    0x18, 0x18, 0x18, 0x70, 0x00, 0x00, 0x00, 0x00,
    // Символ 126 (~)
    0x00, 0x00, 0x76, 0xDC, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const FONT_INFO DefaultFont = {
    .CharWidth = 8,
    .CharHeight = 16,
    .FirstChar = 32,
    .LastChar = 126,
    .Data = Font8x16_Data
};

// ============================================================================
// Инициализация и режимы
// ============================================================================

void Video_UpdateRect(UINT32 x, UINT32 y, UINT32 w, UINT32 h)
{
    if (!Graphics.BackBuffer) return;

    for (UINT32 row = 0; row < h; row++) {
        UINT32 offset = (y + row) * Graphics.PixelsPerScanLine + x;
        // Копируем строку из RAM в VRAM
        // Это быстрая операция (Burst Write)
        fast_memcpy(
            &Graphics.FramebufferBase[offset], 
            &Graphics.BackBuffer[offset], 
            w * 4
        );
    }
}

void Video_SwapBuffers(void)
{
    if (!Graphics.BackBuffer) return;
    fast_memcpy(
        Graphics.FramebufferBase, 
        Graphics.BackBuffer, 
        Graphics.FramebufferSize
    );
}

EFI_STATUS InitGop(void)
{
    EFI_STATUS Status;
    
    Status = BS->LocateProtocol(&GopGuid, NULL, (VOID **)&Gop);
    
    if (EFI_ERROR(Status)) {
        Print(L"GOP not found: %r\r\n", Status);
        return Status;
    }
    
    // Устанавливаем шрифт по умолчанию
    SetDefaultFont();
    
    return EFI_SUCCESS;
}

void ListModes(void)
{
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
    UINTN InfoSize;
    
    Print(L"Available video modes:\r\n");
    
    for (UINT32 i = 0; i < Gop->Mode->MaxMode; i++) {
        EFI_STATUS Status = Gop->QueryMode(Gop, i, &InfoSize, &Info);
        
        if (EFI_ERROR(Status)) continue;
        
        CHAR16 *Format;
        switch (Info->PixelFormat) {
            case PixelRedGreenBlueReserved8BitPerColor: Format = L"RGBX"; break;
            case PixelBlueGreenRedReserved8BitPerColor: Format = L"BGRX"; break;
            case PixelBitMask: Format = L"Mask"; break;
            case PixelBltOnly: Format = L"BltOnly"; break;
            default: Format = L"Unknown";
        }
        
        Print(L"%4u: %ux%u %s%s\r\n", i, 
              Info->HorizontalResolution, Info->VerticalResolution,
              Format, (i == Gop->Mode->Mode) ? L" *" : L"");
    }
}

UINT32 FindBestMode(UINT32 PreferredWidth, UINT32 PreferredHeight)
{
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
    UINTN InfoSize;
    UINT32 BestMode = Gop->Mode->Mode;
    UINT32 BestScore = 0;
    
    for (UINT32 i = 0; i < Gop->Mode->MaxMode; i++) {
        if (EFI_ERROR(Gop->QueryMode(Gop, i, &InfoSize, &Info))) continue;
        if (Info->PixelFormat == PixelBltOnly) continue;
        
        if (Info->HorizontalResolution == PreferredWidth &&
            Info->VerticalResolution == PreferredHeight) {
            return i;
        }
        
        UINT32 Score = Info->HorizontalResolution * Info->VerticalResolution;
        if (Score > BestScore) {
            BestScore = Score;
            BestMode = i;
        }
    }
    
    return BestMode;
}

EFI_STATUS SetGraphicsMode(UINT32 Mode)
{
    EFI_STATUS Status = Gop->SetMode(Gop, Mode);
    if (EFI_ERROR(Status)) return Status;

    Graphics.FramebufferBase = (UINT32 *)Gop->Mode->FrameBufferBase;
    Graphics.FramebufferSize = Gop->Mode->FrameBufferSize;
    Graphics.Width = Gop->Mode->Info->HorizontalResolution;
    Graphics.Height = Gop->Mode->Info->VerticalResolution;
    Graphics.PixelsPerScanLine = Gop->Mode->Info->PixelsPerScanLine;
    Graphics.PixelFormat = Gop->Mode->Info->PixelFormat;
    
    // Очищаем буфер
    if (Graphics.BackBuffer) {
        for (UINTN i = 0; i < Graphics.FramebufferSize / 4; i++) {
            Graphics.BackBuffer[i] = 0xFF000000; // Черный
        }
    }
    // ----------------------------

    return EFI_SUCCESS;
}

// ============================================================================
// Базовые графические функции
// ============================================================================

void FillScreen(UINT32 Color)
{
    for (UINTN y = 0; y < Graphics.Height; y++) {
        for (UINTN x = 0; x < Graphics.Width; x++) {
            Graphics.FramebufferBase[y * Graphics.PixelsPerScanLine + x] = Color;
        }
    }
}

void PutPixel(UINT32 X, UINT32 Y, UINT32 Color)
{
    if (X >= Graphics.Width || Y >= Graphics.Height) return;
    
    UINT32 offset = Y * Graphics.PixelsPerScanLine + X;
    
    // 1. Пишем в VRAM (чтобы видеть текст сразу)
    Graphics.FramebufferBase[offset] = Color;
    
    // 2. ВАЖНО: Пишем в BackBuffer (чтобы сохранить текст для скроллинга)
    if (Graphics.BackBuffer) {
        Graphics.BackBuffer[offset] = Color;
    }
}

void DrawRect(UINT32 X, UINT32 Y, UINT32 W, UINT32 H, UINT32 Color)
{
    for (UINT32 dy = 0; dy < H; dy++) {
        for (UINT32 dx = 0; dx < W; dx++) {
            PutPixel(X + dx, Y + dy, Color);
        }
    }
}

void DrawRectOutline(UINT32 X, UINT32 Y, UINT32 W, UINT32 H, UINT32 Color, UINT32 Thickness)
{
    // Верх
    DrawRect(X, Y, W, Thickness, Color);
    // Низ
    DrawRect(X, Y + H - Thickness, W, Thickness, Color);
    // Лево
    DrawRect(X, Y, Thickness, H, Color);
    // Право
    DrawRect(X + W - Thickness, Y, Thickness, H, Color);
}

void DrawLine(UINT32 X1, UINT32 Y1, UINT32 X2, UINT32 Y2, UINT32 Color)
{
    INT32 dx = (X2 > X1) ? (X2 - X1) : (X1 - X2);
    INT32 dy = (Y2 > Y1) ? (Y2 - Y1) : (Y1 - Y2);
    INT32 sx = (X1 < X2) ? 1 : -1;
    INT32 sy = (Y1 < Y2) ? 1 : -1;
    INT32 err = dx - dy;
    
    while (1) {
        PutPixel(X1, Y1, Color);
        
        if (X1 == X2 && Y1 == Y2) break;
        
        INT32 e2 = 2 * err;
        if (e2 > -dy) { err -= dy; X1 += sx; }
        if (e2 < dx)  { err += dx; Y1 += sy; }
    }
}

UINT32 MakeColor(UINT8 R, UINT8 G, UINT8 B)
{
    if (Graphics.PixelFormat == PixelRedGreenBlueReserved8BitPerColor) {
        return (R << 0) | (G << 8) | (B << 16);
    } else {
        return (B << 0) | (G << 8) | (R << 16);
    }
}

// ============================================================================
// Рисование из байтов (сокращённые версии)
// ============================================================================

void DrawRGB24(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, const UINT8 *Data)
{
    for (UINT32 dy = 0; dy < Height; dy++) {
        for (UINT32 dx = 0; dx < Width; dx++) {
            UINTN Offset = (dy * Width + dx) * 3;
            PutPixel(X + dx, Y + dy, MakeColor(Data[Offset], Data[Offset+1], Data[Offset+2]));
        }
    }
}

void DrawRGBA32(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, const UINT8 *Data)
{
    for (UINT32 dy = 0; dy < Height; dy++) {
        for (UINT32 dx = 0; dx < Width; dx++) {
            UINTN Offset = (dy * Width + dx) * 4;
            PutPixel(X + dx, Y + dy, MakeColor(Data[Offset], Data[Offset+1], Data[Offset+2]));
        }
    }
}

void DrawMono1BPP(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height,
                  const UINT8 *Data, UINT32 FgColor, UINT32 BgColor)
{
    UINT32 BytesPerRow = (Width + 7) / 8;
    
    for (UINT32 dy = 0; dy < Height; dy++) {
        for (UINT32 dx = 0; dx < Width; dx++) {
            UINT32 ByteIndex = dy * BytesPerRow + dx / 8;
            UINT32 BitIndex = 7 - (dx % 8);
            UINT8 Bit = (Data[ByteIndex] >> BitIndex) & 1;
            
            if (Bit) {
                PutPixel(X + dx, Y + dy, FgColor);
            } else if (BgColor != TRANSPARENT_BG) {
                PutPixel(X + dx, Y + dy, BgColor);
            }
        }
    }
}

// ... (остальные Draw функции аналогично вашему коду)

// ============================================================================
// BMP функции
// ============================================================================

BOOLEAN ParseBMP(const UINT8 *Data, UINTN DataSize, BMP_IMAGE *Image)
{
    if (DataSize < sizeof(BMP_FILE_HEADER) + sizeof(BMP_INFO_HEADER)) return FALSE;
    
    const BMP_FILE_HEADER *FileHeader = (const BMP_FILE_HEADER *)Data;
    const BMP_INFO_HEADER *InfoHeader = (const BMP_INFO_HEADER *)(Data + sizeof(BMP_FILE_HEADER));
    
    if (FileHeader->Signature != 0x4D42) return FALSE;
    if (InfoHeader->Compression != 0) return FALSE;
    
    Image->Width = (InfoHeader->Width < 0) ? -InfoHeader->Width : InfoHeader->Width;
    Image->Height = (InfoHeader->Height < 0) ? -InfoHeader->Height : InfoHeader->Height;
    Image->BitsPerPixel = InfoHeader->BitsPerPixel;
    Image->TopDown = (InfoHeader->Height < 0);
    Image->RowSize = ((Image->Width * Image->BitsPerPixel + 31) / 32) * 4;
    
    if (Image->BitsPerPixel <= 8) {
        Image->PaletteSize = InfoHeader->ColorsUsed ? InfoHeader->ColorsUsed : (1 << Image->BitsPerPixel);
        Image->Palette = Data + sizeof(BMP_FILE_HEADER) + InfoHeader->HeaderSize;
    } else {
        Image->Palette = NULL;
        Image->PaletteSize = 0;
    }
    
    Image->PixelData = Data + FileHeader->DataOffset;
    return TRUE;
}

static UINT32 GetBMPPixel(const BMP_IMAGE *Image, UINT32 X, UINT32 Y)
{
    UINT32 Row = Image->TopDown ? Y : (Image->Height - 1 - Y);
    const UINT8 *RowData = Image->PixelData + Row * Image->RowSize;
    UINT8 R, G, B;
    
    switch (Image->BitsPerPixel) {
        case 32:
        case 24: {
            UINT32 Bpp = Image->BitsPerPixel / 8;
            B = RowData[X * Bpp + 0];
            G = RowData[X * Bpp + 1];
            R = RowData[X * Bpp + 2];
            break;
        }
        case 8: {
            UINT8 Index = RowData[X];
            B = Image->Palette[Index * 4 + 0];
            G = Image->Palette[Index * 4 + 1];
            R = Image->Palette[Index * 4 + 2];
            break;
        }
        default:
            R = G = B = 0;
    }
    
    return MakeColor(R, G, B);
}

BOOLEAN DrawBMP(UINT32 X, UINT32 Y, const UINT8 *Data, UINTN DataSize)
{
    BMP_IMAGE Image;
    if (!ParseBMP(Data, DataSize, &Image)) return FALSE;
    
    for (UINT32 dy = 0; dy < Image.Height; dy++) {
        for (UINT32 dx = 0; dx < Image.Width; dx++) {
            PutPixel(X + dx, Y + dy, GetBMPPixel(&Image, dx, dy));
        }
    }
    return TRUE;
}

BOOLEAN DrawBMPFast(UINT32 X, UINT32 Y, const UINT8 *Data, UINTN DataSize)
{
    return DrawBMP(X, Y, Data, DataSize);
}

BOOLEAN DrawBMPScaled(UINT32 X, UINT32 Y, UINT32 DstW, UINT32 DstH, const UINT8 *Data, UINTN DataSize)
{
    BMP_IMAGE Image;
    if (!ParseBMP(Data, DataSize, &Image)) return FALSE;
    
    for (UINT32 dy = 0; dy < DstH; dy++) {
        for (UINT32 dx = 0; dx < DstW; dx++) {
            UINT32 SrcX = (dx * Image.Width) / DstW;
            UINT32 SrcY = (dy * Image.Height) / DstH;
            PutPixel(X + dx, Y + dy, GetBMPPixel(&Image, SrcX, SrcY));
        }
    }
    return TRUE;
}

BOOLEAN GetBMPSize(const UINT8 *Data, UINTN DataSize, UINT32 *Width, UINT32 *Height)
{
    BMP_IMAGE Image;
    if (!ParseBMP(Data, DataSize, &Image)) return FALSE;
    *Width = Image.Width;
    *Height = Image.Height;
    return TRUE;
}

// ============================================================================
// ТЕКСТОВЫЕ ФУНКЦИИ
// ============================================================================

void SetFont(const FONT_INFO *Font)
{
    if (Font != NULL) {
        CurrentFont = *Font;
    }
}

void SetDefaultFont(void)
{
    CurrentFont = DefaultFont;
}

void DrawChar(UINT32 X, UINT32 Y, CHAR8 Ch, UINT32 FgColor, UINT32 BgColor)
{
    if (Ch < CurrentFont.FirstChar || Ch > CurrentFont.LastChar) Ch = '?';
    
    // Получаем указатели
    UINT32 CharIndex = Ch - CurrentFont.FirstChar;
    const UINT8 *CharData = CurrentFont.Data + CharIndex * CurrentFont.CharHeight;
    
    UINT32 *buffer = Graphics.BackBuffer ? Graphics.BackBuffer : Graphics.FramebufferBase;
    UINT32 ppl = Graphics.PixelsPerScanLine;

    // Рисуем прямо в буфер
    for (UINT32 row = 0; row < CurrentFont.CharHeight; row++) {
        // Вычисляем адрес начала строки в буфере
        // (Оптимизация: вынести Y * ppl за цикл)
        UINT32 *line_ptr = &buffer[(Y + row) * ppl + X];
        UINT8 RowBits = CharData[row];
        
        // Разворачиваем цикл для стандартной ширины 8
        if (CurrentFont.CharWidth == 8) {
            line_ptr[0] = (RowBits & 0x80) ? FgColor : BgColor;
            line_ptr[1] = (RowBits & 0x40) ? FgColor : BgColor;
            line_ptr[2] = (RowBits & 0x20) ? FgColor : BgColor;
            line_ptr[3] = (RowBits & 0x10) ? FgColor : BgColor;
            line_ptr[4] = (RowBits & 0x08) ? FgColor : BgColor;
            line_ptr[5] = (RowBits & 0x04) ? FgColor : BgColor;
            line_ptr[6] = (RowBits & 0x02) ? FgColor : BgColor;
            line_ptr[7] = (RowBits & 0x01) ? FgColor : BgColor;
        } else {
            // Старый цикл для других шрифтов
            for (UINT32 col = 0; col < CurrentFont.CharWidth; col++) {
                if ((RowBits >> (7 - col)) & 1) line_ptr[col] = FgColor;
                else if (BgColor != TRANSPARENT_BG) line_ptr[col] = BgColor;
            }
        }
    }

    // Если рисовали в BackBuffer, нужно скопировать этот квадратик в VRAM
    if (Graphics.BackBuffer) {
        Video_UpdateRect(X, Y, CurrentFont.CharWidth, CurrentFont.CharHeight);
    }
}

void DrawCharScaled(UINT32 X, UINT32 Y, CHAR8 Ch, UINT32 FgColor, UINT32 BgColor, UINT32 Scale)
{
    if (Scale == 0) Scale = 1;
    
    if (Ch < CurrentFont.FirstChar || Ch > CurrentFont.LastChar) {
        Ch = '?';
        if (Ch < CurrentFont.FirstChar || Ch > CurrentFont.LastChar) return;
    }
    
    UINT32 CharIndex = Ch - CurrentFont.FirstChar;
    const UINT8 *CharData = CurrentFont.Data + CharIndex * CurrentFont.CharHeight;
    
    for (UINT32 row = 0; row < CurrentFont.CharHeight; row++) {
        UINT8 RowBits = CharData[row];
        
        for (UINT32 col = 0; col < CurrentFont.CharWidth; col++) {
            UINT8 Bit = (RowBits >> (7 - col)) & 1;
            UINT32 Color = Bit ? FgColor : BgColor;
            
            if (Bit || BgColor != TRANSPARENT_BG) {
                DrawRect(X + col * Scale, Y + row * Scale, Scale, Scale, Color);
            }
        }
    }
}

void DrawString(UINT32 X, UINT32 Y, const CHAR8 *Str, UINT32 FgColor, UINT32 BgColor)
{
    UINT32 CurX = X;
    
    while (*Str) {
        if (*Str == '\n') {
            CurX = X;
            Y += CurrentFont.CharHeight;
        } else if (*Str == '\r') {
            CurX = X;
        } else if (*Str == '\t') {
            CurX += CurrentFont.CharWidth * 4;
        } else {
            DrawChar(CurX, Y, *Str, FgColor, BgColor);
            CurX += CurrentFont.CharWidth;
        }
        Str++;
    }
}

void DrawStringScaled(UINT32 X, UINT32 Y, const CHAR8 *Str, UINT32 FgColor, UINT32 BgColor, UINT32 Scale)
{
    if (Scale == 0) Scale = 1;
    UINT32 CurX = X;
    UINT32 CharW = CurrentFont.CharWidth * Scale;
    UINT32 CharH = CurrentFont.CharHeight * Scale;
    
    while (*Str) {
        if (*Str == '\n') {
            CurX = X;
            Y += CharH;
        } else if (*Str == '\r') {
            CurX = X;
        } else if (*Str == '\t') {
            CurX += CharW * 4;
        } else {
            DrawCharScaled(CurX, Y, *Str, FgColor, BgColor, Scale);
            CurX += CharW;
        }
        Str++;
    }
}

void DrawStringW(UINT32 X, UINT32 Y, const CHAR16 *Str, UINT32 FgColor, UINT32 BgColor)
{
    UINT32 CurX = X;
    
    while (*Str) {
        if (*Str == L'\n') {
            CurX = X;
            Y += CurrentFont.CharHeight;
        } else if (*Str == L'\r') {
            CurX = X;
        } else if (*Str == L'\t') {
            CurX += CurrentFont.CharWidth * 4;
        } else {
            // Конвертируем CHAR16 в CHAR8 (только ASCII)
            CHAR8 Ch = (*Str < 128) ? (CHAR8)*Str : '?';
            DrawChar(CurX, Y, Ch, FgColor, BgColor);
            CurX += CurrentFont.CharWidth;
        }
        Str++;
    }
}

void DrawStringWScaled(UINT32 X, UINT32 Y, const CHAR16 *Str, UINT32 FgColor, UINT32 BgColor, UINT32 Scale)
{
    if (Scale == 0) Scale = 1;
    UINT32 CurX = X;
    UINT32 CharW = CurrentFont.CharWidth * Scale;
    UINT32 CharH = CurrentFont.CharHeight * Scale;
    
    while (*Str) {
        if (*Str == L'\n') {
            CurX = X;
            Y += CharH;
        } else if (*Str == L'\r') {
            CurX = X;
        } else if (*Str == L'\t') {
            CurX += CharW * 4;
        } else {
            CHAR8 Ch = (*Str < 128) ? (CHAR8)*Str : '?';
            DrawCharScaled(CurX, Y, Ch, FgColor, BgColor, Scale);
            CurX += CharW;
        }
        Str++;
    }
}

UINT32 GetStringWidth(const CHAR8 *Str)
{
    UINT32 Width = 0;
    UINT32 MaxWidth = 0;
    
    while (*Str) {
        if (*Str == '\n') {
            if (Width > MaxWidth) MaxWidth = Width;
            Width = 0;
        } else if (*Str != '\r') {
            Width += CurrentFont.CharWidth;
        }
        Str++;
    }
    
    return (Width > MaxWidth) ? Width : MaxWidth;
}

UINT32 GetStringWidthW(const CHAR16 *Str)
{
    UINT32 Width = 0;
    UINT32 MaxWidth = 0;
    
    while (*Str) {
        if (*Str == L'\n') {
            if (Width > MaxWidth) MaxWidth = Width;
            Width = 0;
        } else if (*Str != L'\r') {
            Width += CurrentFont.CharWidth;
        }
        Str++;
    }
    
    return (Width > MaxWidth) ? Width : MaxWidth;
}

UINT32 GetFontHeight(void)
{
    return CurrentFont.CharHeight;
}

void DrawStringCentered(UINT32 Y, const CHAR8 *Str, UINT32 FgColor, UINT32 BgColor)
{
    UINT32 Width = GetStringWidth(Str);
    UINT32 X = (Graphics.Width - Width) / 2;
    DrawString(X, Y, Str, FgColor, BgColor);
}

void DrawStringWCentered(UINT32 Y, const CHAR16 *Str, UINT32 FgColor, UINT32 BgColor)
{
    UINT32 Width = GetStringWidthW(Str);
    UINT32 X = (Graphics.Width - Width) / 2;
    DrawStringW(X, Y, Str, FgColor, BgColor);
}