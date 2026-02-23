#ifndef BASE_GRAPHIC_H
#define BASE_GRAPHIC_H

#include <efi.h>
#include <efilib.h>

// ============================================================================
// Структуры
// ============================================================================

// Информация о графике
typedef struct {
    UINT32 *FramebufferBase; // Указатель на VRAM (медленный на чтение)
    UINT32 *BackBuffer;      // Указатель на RAM (быстрый)
    UINTN  FramebufferSize;
    UINT32 Width;
    UINT32 Height;
    UINT32 PixelsPerScanLine;
    UINT32 PixelFormat;
} GRAPHICS_INFO;

// BMP File Header (14 байт)
typedef struct {
    UINT16 Signature;
    UINT32 FileSize;
    UINT16 Reserved1;
    UINT16 Reserved2;
    UINT32 DataOffset;
} __attribute__((packed)) BMP_FILE_HEADER;

// BMP Info Header (40 байт)
typedef struct {
    UINT32 HeaderSize;
    INT32  Width;
    INT32  Height;
    UINT16 Planes;
    UINT16 BitsPerPixel;
    UINT32 Compression;
    UINT32 ImageSize;
    INT32  XPixelsPerMeter;
    INT32  YPixelsPerMeter;
    UINT32 ColorsUsed;
    UINT32 ColorsImportant;
} __attribute__((packed)) BMP_INFO_HEADER;

// Информация о загруженном BMP
typedef struct {
    UINT32 Width;
    UINT32 Height;
    UINT16 BitsPerPixel;
    BOOLEAN TopDown;
    const UINT8 *PixelData;
    const UINT8 *Palette;
    UINT32 PaletteSize;
    UINT32 RowSize;
} BMP_IMAGE;

// Информация о шрифте
typedef struct {
    UINT32 CharWidth;       // Ширина символа в пикселях
    UINT32 CharHeight;      // Высота символа в пикселях
    UINT32 FirstChar;       // Первый символ (обычно 32 = пробел)
    UINT32 LastChar;        // Последний символ (обычно 126 = ~)
    const UINT8 *Data;      // Битовые данные шрифта
} FONT_INFO;

// ============================================================================
// Глобальные переменные
// ============================================================================

extern EFI_GUID GopGuid;
extern GRAPHICS_INFO Graphics;
extern EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop;
extern FONT_INFO CurrentFont;

// ============================================================================
// Константы для текста
// ============================================================================

#define TRANSPARENT_BG 0xFFFFFFFF

// ============================================================================
// Инициализация и режимы
// ============================================================================

void Video_UpdateRect(UINT32 x, UINT32 y, UINT32 w, UINT32 h);
void Video_SwapBuffers(void);

EFI_STATUS InitGop(void);
void ListModes(void);
UINT32 FindBestMode(UINT32 PreferredWidth, UINT32 PreferredHeight);
EFI_STATUS SetGraphicsMode(UINT32 Mode);

// ============================================================================
// Базовые графические функции
// ============================================================================

void FillScreen(UINT32 Color);
void PutPixel(UINT32 X, UINT32 Y, UINT32 Color);
void DrawRect(UINT32 X, UINT32 Y, UINT32 W, UINT32 H, UINT32 Color);
void DrawRectOutline(UINT32 X, UINT32 Y, UINT32 W, UINT32 H, UINT32 Color, UINT32 Thickness);
void DrawLine(UINT32 X1, UINT32 Y1, UINT32 X2, UINT32 Y2, UINT32 Color);
UINT32 MakeColor(UINT8 R, UINT8 G, UINT8 B);

// ============================================================================
// Рисование из байтов
// ============================================================================

void DrawRGB24(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, const UINT8 *Data);
void DrawRGBA32(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, const UINT8 *Data);
void DrawRGBA32WithAlpha(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, const UINT8 *Data);
void DrawBGR24(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, const UINT8 *Data);
void DrawMono1BPP(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, const UINT8 *Data, UINT32 FgColor, UINT32 BgColor);
void DrawGrayscale8(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, const UINT8 *Data);
void DrawIndexed8(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, const UINT8 *Data, const UINT8 *Palette);
void BlitRaw32(UINT32 X, UINT32 Y, UINT32 Width, UINT32 Height, const UINT32 *Data);
void DrawRGB24Scaled(UINT32 X, UINT32 Y, UINT32 SrcWidth, UINT32 SrcHeight, UINT32 DstWidth, UINT32 DstHeight, const UINT8 *Data);

// ============================================================================
// BMP функции
// ============================================================================

BOOLEAN ParseBMP(const UINT8 *Data, UINTN DataSize, BMP_IMAGE *Image);
BOOLEAN DrawBMP(UINT32 X, UINT32 Y, const UINT8 *Data, UINTN DataSize);
BOOLEAN DrawBMPFast(UINT32 X, UINT32 Y, const UINT8 *Data, UINTN DataSize);
BOOLEAN DrawBMPScaled(UINT32 X, UINT32 Y, UINT32 DstWidth, UINT32 DstHeight, const UINT8 *Data, UINTN DataSize);
BOOLEAN GetBMPSize(const UINT8 *Data, UINTN DataSize, UINT32 *Width, UINT32 *Height);

// ============================================================================
// Текстовые функции
// ============================================================================

// Установка шрифта
void SetFont(const FONT_INFO *Font);
void SetDefaultFont(void);

// Рисование одного символа
void DrawChar(UINT32 X, UINT32 Y, CHAR8 Ch, UINT32 FgColor, UINT32 BgColor);
void DrawCharScaled(UINT32 X, UINT32 Y, CHAR8 Ch, UINT32 FgColor, UINT32 BgColor, UINT32 Scale);

// Рисование строки ASCII
void DrawString(UINT32 X, UINT32 Y, const CHAR8 *Str, UINT32 FgColor, UINT32 BgColor);
void DrawStringScaled(UINT32 X, UINT32 Y, const CHAR8 *Str, UINT32 FgColor, UINT32 BgColor, UINT32 Scale);

// Рисование строки Unicode (CHAR16)
void DrawStringW(UINT32 X, UINT32 Y, const CHAR16 *Str, UINT32 FgColor, UINT32 BgColor);
void DrawStringWScaled(UINT32 X, UINT32 Y, const CHAR16 *Str, UINT32 FgColor, UINT32 BgColor, UINT32 Scale);

// Форматированный вывод (как Print, но графический)
void DrawPrintf(UINT32 X, UINT32 Y, UINT32 FgColor, UINT32 BgColor, const CHAR8 *Format, ...);

// Получение размеров текста
UINT32 GetStringWidth(const CHAR8 *Str);
UINT32 GetStringWidthW(const CHAR16 *Str);
UINT32 GetFontHeight(void);

// Центрирование текста
void DrawStringCentered(UINT32 Y, const CHAR8 *Str, UINT32 FgColor, UINT32 BgColor);
void DrawStringWCentered(UINT32 Y, const CHAR16 *Str, UINT32 FgColor, UINT32 BgColor);

#endif // GOP_PROTOCOL_H