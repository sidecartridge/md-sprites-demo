#ifndef VGA_H_FILE
#define VGA_H_FILE

#include <stdbool.h>
#include <stdint.h>

#define VGA_ERROR_ALLOC (-1)
#define VGA_ERROR_MULTICORE (-2)

#ifdef __cplusplus
extern "C" {
#endif

struct VGA_MODE {
  unsigned short h_pixels;
  unsigned short v_pixels;
  unsigned char color_bits;  // Number of bits per pixel
};

struct VGA_SCREEN {
  /* Pointers first for alignment/cache friendliness */
  unsigned int *framebuffer_a;
  unsigned int *framebuffer_b;
  unsigned int *current_framebuffer;
  unsigned int *hidden_framebuffer;
  /* Geometry (fit in 16 bits if constraints allow) */
  uint16_t width;
  uint16_t height;
  /* Bit depth and front/back ids */
  uint8_t color_bits; /* Number of bits per pixel */
  uint8_t current_framebuffer_id;
  uint8_t hidden_framebuffer_id;
  uint8_t _pad; /* Explicit pad to keep size multiple of 4 if needed */
};

/* Global screen state (defined in vga.c) */
extern struct VGA_SCREEN vga_screen;

#if VGA_ENABLE_MULTICORE
extern void (*volatile vga_core1_func)(void);
#endif

void vga_clear_screen();
int vga_init(const struct VGA_MODE *mode, uint32_t framebuffer_a,
             uint32_t framebuffer_b);
/*
 * Swap the visible (current) and hidden framebuffers.
 * Implemented as always_inline for maximum performance at call sites.
 */
static inline __attribute__((always_inline)) void vga_swap_framebuffers(void) {
  unsigned int *cur = vga_screen.current_framebuffer;
  vga_screen.current_framebuffer = vga_screen.hidden_framebuffer;
  vga_screen.hidden_framebuffer = cur;
  uint8_t id = vga_screen.current_framebuffer_id;
  vga_screen.current_framebuffer_id = vga_screen.hidden_framebuffer_id;
  vga_screen.hidden_framebuffer_id = id;
  /* Optional memory barrier if another core / DMA reads immediately */
  // __asm volatile("" ::: "memory");
}

/* Accessors (inline for potential future safety checks) */
static inline __attribute__((always_inline)) unsigned int *vga_get_frontbuffer(
    void) {
  return vga_screen.current_framebuffer;
}
static inline __attribute__((always_inline)) unsigned int *vga_get_backbuffer(
    void) {
  return vga_screen.hidden_framebuffer;
}
static inline __attribute__((always_inline)) uint8_t
vga_get_frontbuffer_id(void) {
  return vga_screen.current_framebuffer_id;
}
static inline __attribute__((always_inline)) uint8_t
vga_get_backbuffer_id(void) {
  return vga_screen.hidden_framebuffer_id;
}

void vga_copy_to_display(uint32_t cartridge_fb, void *code_address,
                         uint32_t st_screen_address);

extern const struct VGA_MODE vga_mode_320x200;

#ifdef __cplusplus
}
#endif

#endif /* VGA_H_FILE */