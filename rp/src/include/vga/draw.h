#ifndef VGA_DRAW_H_FILE
#define VGA_DRAW_H_FILE

#include <stdbool.h>

#include "debug.h"
#include "vga.h"

/* Size of precomputed per-pixel mask table: 16 palette indices * 16 x positions
 */
#define VGA_PIXEL_MASK_TABLE_SIZE 256
/* Number of bitplanes currently assumed */
#define VGA_NUM_BITPLANES 4
/* Pixels per 32-bit packed group (one word = 4 pixels) */
#define VGA_GROUP_PIXELS 4
/* Pixels per 64-bit block group (mask granularity) */
#define VGA_BLOCK_PIXELS 16
/* Reserved bottom screen offset (status bar etc.) */
#define VGA_STATUS_BAR_OFFSET 8
/* Mask for packed 6-bit-per-channel (B,G,R) indices (each byte low 6 bits) */
#define VGA_RGB6_PACK_MASK 0x3F3F3F3Fu

/* Expose precomputed pixel masks table for use in font & sprite rendering.
 * Layout index: (palette_index << 4) | pixel_x (0..15)
 * Special palette_index 0xF used as clear mask (all planes) in existing code.
 */
extern uint64_t pixel_masks_flat[VGA_PIXEL_MASK_TABLE_SIZE];

#ifdef __cplusplus
extern "C" {
#endif

/* Sprite descriptor: width/height in pixels, stride in 32-bit words per row.
 * Data points to packed 4-pixel (32-bit) groups; stride accounts for padding.
 */
struct SPRITE {
  int width;
  int height;
  unsigned int stride;      /* number of 32-bit words per line */
  const unsigned int *data; /* immutable pixel data */
};

void __not_in_flash_func(init_pixel_masks)(void);

/* Sprite drawing core helpers (implemented in vga_draw.c) */
void __not_in_flash_func(draw_sprite_transparent)(const struct SPRITE *spr,
                                                  int spr_x, int spr_y);
void __not_in_flash_func(draw_sprite_opaque)(const struct SPRITE *spr,
                                             int spr_x, int spr_y);

void __not_in_flash_func(draw_tile)(const struct SPRITE *__restrict spr,
                                    int spr_x, int spr_y);

/* Inline small dispatcher */
static inline void __not_in_flash_func(draw_sprite)(
    const struct SPRITE *__restrict sprite, int spr_x, int spr_y,
    bool transparent) {
  if (transparent)
    draw_sprite_transparent(sprite, spr_x, spr_y);
  else
    draw_sprite_opaque(sprite, spr_x, spr_y);
}

void __not_in_flash_func(draw_show_color_index)(unsigned int data[],
                                                int img_tiles_num);

/* Convert generated img_tiles_data (tiles.h) into Atari ST planar format array
 * image_tiles_data_st (defined in vga_draw.c). Safe to call multiple times. */
void __not_in_flash_func(convert_tiles_to_st_planar)(void);
#ifdef __cplusplus
}
#endif

#endif /* VGA_DRAW_H_FILE */