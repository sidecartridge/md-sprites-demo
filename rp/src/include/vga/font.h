#ifndef VGA_FONT_H_FILE
#define VGA_FONT_H_FILE

#define VGA_FONT_USE_STDARG 1

#include <pico.h>
#include <stdint.h>

#include "vga.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Font descriptor (glyphs are monochrome bitmaps) */
struct VGA_FONT {
  int w;                     /* glyph width in pixels */
  int h;                     /* glyph height in pixels */
  int first_char;            /* first character code represented */
  int num_chars;             /* number of sequential characters */
  const unsigned char *data; /* bitmap rows: (h rows) * num_chars */
};

enum FONT_ALIGNMENT { FONT_ALIGN_LEFT, FONT_ALIGN_CENTER, FONT_ALIGN_RIGHT };

/* Border color mask (current implementation uses 5 bits) */
#define FONT_BORDER_COLOR_MASK 0x1F
/* Derive active color mask from current video mode */
static inline __attribute__((always_inline)) unsigned int
font_active_color_mask(void) {
  return (1u << vga_screen.color_bits) - 1u;
}

/* State (defined in vga_font.c) */
extern struct VGA_FONT const *font;
extern unsigned int font_x, font_y;
extern enum FONT_ALIGNMENT font_alignment;
extern unsigned char font_color;
extern unsigned char border[2];

/* Inline trivial setters (kept in RAM) */
static inline void __not_in_flash_func(font_set_font)(
    const struct VGA_FONT *newFont) {
  font = newFont;
}
static inline void __not_in_flash_func(font_set_color)(unsigned int fgColor) {
  font_color = (unsigned char)(fgColor & font_active_color_mask());
}
static inline void __not_in_flash_func(font_set_border)(
    int enableBorder, unsigned int borderColor) {
  border[0] = (unsigned char)enableBorder;
  border[1] = (unsigned char)(borderColor & FONT_BORDER_COLOR_MASK);
}
static inline void __not_in_flash_func(font_move)(unsigned int pos_x,
                                                  unsigned int pos_y) {
  font_x = pos_x;
  font_y = pos_y;
}
static inline void __not_in_flash_func(font_align)(
    enum FONT_ALIGNMENT alignment) {
  font_alignment = alignment;
}

void __not_in_flash_func(font_print_int)(int num);
void __not_in_flash_func(font_print_uint)(unsigned int num);
void __not_in_flash_func(font_print_float)(float num);
void __not_in_flash_func(font_print)(const char *text);

#if VGA_FONT_USE_STDARG
void __not_in_flash_func(font_printf)(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)));
#endif

#ifdef __cplusplus
}
#endif

#endif /* VGA_FONT_H_FILE */