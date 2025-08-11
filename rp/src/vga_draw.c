#include <stdlib.h>

#include "vga/draw.h"

#ifndef likely
#define likely(x) __builtin_expect(!!(x), 1)
#endif
#ifndef unlikely
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

// plane 0 | plane 1 | plane 2 | plane 3
// Place in scratch X bank to reduce contention with rgb2index in scratch Y.
uint64_t pixel_masks_flat[256] __attribute__((
    aligned(8),
    section(".scratch_x.pixel_masks")));  // flattened [palette<<4 | pixel_x]

void __not_in_flash_func(init_pixel_masks)(void) {
  for (int index = 0; index < 16; ++index) {
    for (int x = 0; x < 16; ++x) {
      uint64_t mask = 0;
      for (int plane = 0; plane < 4; ++plane) {
        if (index & (1 << plane)) {
          mask |= (uint64_t)1 << (plane * 16 + (15 - x));
        }
      }
      int flat = (index << 4) | x;
      pixel_masks_flat[flat] = mask;
      // DPRINTF("pixel_masks[%d] = 0x%016llX\n", flat, mask);
    }
  }
}

/* rgb2index LUT in opposite scratch bank (Y) to pixel_masks_flat (X) */
static uint16_t rgb2index[64]
    __attribute__((aligned(2), section(".scratch_y.rgb2index"))) = {
        0,   // BGR: 0b00000000 -> Index: 0
        1,   // BGR: 0b00000001 -> Index: 1
        2,   // BGR: 0b00000010 -> Index: 2
        3,   // BGR: 0b00000011 -> Index: 3
        4,   // BGR: 0b00000100 -> Index: 4
        5,   // BGR: 0b00000101 -> Index: 5
        6,   // BGR: 0b00000110 -> Index: 6
        7,   // BGR: 0b00000111 -> Index: 7
        15,  // BGR: 0b00001000 -> Index: 15
        15,  // BGR: 0b00001001 -> Index: 15
        15,  // BGR: 0b00001010 -> Index: 15
        11,  // BGR: 0b00001011 -> Index: 11
        12,  // BGR: 0b00001100 -> Index: 12
        15,  // BGR: 0b00001101 -> Index: 15
        15,  // BGR: 0b00001110 -> Index: 15
        15,  // BGR: 0b00001111 -> Index: 15
        8,   // BGR: 0b00010000 -> Index: 8
        15,  // BGR: 0b00010001 -> Index: 15
        15,  // BGR: 0b00010010 -> Index: 15
        15,  // BGR: 0b00010011 -> Index: 15
        9,   // BGR: 0b00010100 -> Index: 9
        10,  // BGR: 0b00010101 -> Index: 10
        6,   // BGR: 0b00010110 -> Index: 6
        14,  // BGR: 0b00010111 -> Index: 14
        15,  // BGR: 0b00011000 -> Index: 15
        15,  // BGR: 0b00011001 -> Index: 15
        15,  // BGR: 0b00011010 -> Index: 15
        11,  // BGR: 0b00011011 -> Index: 11
        15,  // BGR: 0b00011100 -> Index: 15
        15,  // BGR: 0b00011101 -> Index: 15
        15,  // BGR: 0b00011110 -> Index: 15
        15,  // BGR: 0b00011111 -> Index: 15 *
        15,  // BGR: 0b00100000 -> Index: 15
        15,  // BGR: 0b00100001 -> Index: 15
        15,  // BGR: 0b00100010 -> Index: 15
        15,  // BGR: 0b00100011 -> Index: 15
        15,  // BGR: 0b00100100 -> Index: 15
        10,  // BGR: 0b00100101 -> Index: 10
        15,  // BGR: 0b00100110 -> Index: 15
        15,  // BGR: 0b00100111 -> Index: 14
        15,  // BGR: 0b00101000 -> Index: 15
        15,  // BGR: 0b00101001 -> Index: 15
        13,  // BGR: 0b00101010 -> Index: 13
        14,  // BGR: 0b00101011 -> Index: 14
        15,  // BGR: 0b00101100 -> Index: 15
        15,  // BGR: 0b00101101 -> Index: 15
        15,  // BGR: 0b00101110 -> Index: 15
        15,  // BGR: 0b00101111 -> Index: 15 *
        15,  // BGR: 0b00110000 -> Index: 15
        15,  // BGR: 0b00110001 -> Index: 15
        15,  // BGR: 0b00110010 -> Index: 15
        15,  // BGR: 0b00110011 -> Index: 15
        15,  // BGR: 0b00110100 -> Index: 15
        14,  // BGR: 0b00110101 -> Index: 14
        15,  // BGR: 0b00110110 -> Index: 15
        15,  // BGR: 0b00110111 -> Index: 15
        15,  // BGR: 0b00111000 -> Index: 15
        15,  // BGR: 0b00111001 -> Index: 15
        15,  // BGR: 0b00111010 -> Index: 15
        15,  // BGR: 0b00111011 -> Index: 15
        15,  // BGR: 0b00111100 -> Index: 15
        15,  // BGR: 0b00111101 -> Index: 15
        15,  // BGR: 0b00111110 -> Index: 15
        15,  // BGR: 0b00111111 -> Index: 15
};

// comparator for sorting unique unsigned char values
static int compare_uchar(const void *a, const void *b) {
  unsigned char va = *(const unsigned char *)a;
  unsigned char vb = *(const unsigned char *)b;
  return (int)va - (int)vb;
}

void __not_in_flash_func(draw_show_color_index)(unsigned int data[],
                                                int img_tiles_num) {
  unsigned char *unique = malloc(img_tiles_num * sizeof(unsigned char));
  if (unique == NULL) {
    DPRINTF("Failed to allocate memory for unique color index\n");
    return;
  }
  int unique_count = 0;
  for (int i = 0; i < img_tiles_num; i++) {
    unsigned char val = (unsigned char)data[i] & 0x3F;
    bool found = false;
    for (int j = 0; j < unique_count; j++) {
      if (unique[j] == val) {
        found = true;
        break;
      }
    }
    if (!found) {
      unique[unique_count++] = val;
    }
  }
  qsort(unique, unique_count, sizeof(unsigned char), compare_uchar);
  DPRINTF("Unique colors (sorted):\n");
  for (int i = 0; i < unique_count; i++) {
    DPRINTF("  %u: ", (unsigned int)unique[i]);
    // show original 8-bit binary format
    {
      char binbuf[9];
      for (int b = 0; b < 8; b++) {
        binbuf[b] = (unique[i] & (1u << (7 - b))) ? '1' : '0';
      }
      binbuf[8] = '\0';
      DPRINTFRAW("0b%s ", binbuf);
    }
    // print 2-bit channels extended to 3 bits (MSB zero)
    unsigned char raw_b = (unique[i] & 0x3) << 1;         // bits 0-1
    unsigned char raw_g = ((unique[i] >> 2) & 0x3) << 1;  // bits 2-3
    unsigned char raw_r = ((unique[i] >> 4) & 0x3) << 1;  // bits 4-5
    // Or 1 extra bit to make it 3 bits
    raw_b |= 1;
    raw_g |= 1;
    raw_r |= 1;
    DPRINTFRAW("B(%u), G(%u), R(%u): move.w #$%1X%1X%1X\n", (unsigned int)raw_b,
               (unsigned int)raw_g, (unsigned int)raw_r, raw_b, raw_g, raw_r);
  }
  free(unique);
}

/* Transparent path (honors 0xCC sentinel) */
void __not_in_flash_func(draw_sprite_transparent)(const struct SPRITE *spr,
                                                  int spr_x, int spr_y) {
  const unsigned int *image_start = spr->data;
  int height = spr->height;
  if (spr_y < 0) {
    image_start += spr->stride * (-spr_y);
    height += spr_y;
    spr_y = 0;
  }
  if (height > (vga_screen.height - 8) - spr_y)
    height = (vga_screen.height - 8) - spr_y;
  if (height <= 0) return;
  int width = spr->width;
  if (spr_x < 0) {
    image_start += (-spr_x) / 4;
    width += spr_x;
    spr_x = ((unsigned int)spr_x) % 4;
  }
  if (width > vga_screen.width - spr_x) width = vga_screen.width - spr_x;
  if (width <= 0) return;
  int row_bytes = vga_screen.width / (VGA_BLOCK_PIXELS / 2);
  int size_stride = sizeof(*image_start);
  for (int y = 0; y < height; y++) {
    uint8_t *line =
        (uint8_t *)&vga_screen.hidden_framebuffer[(y + spr_y) * row_bytes];
    if ((line - (uint8_t *)vga_screen.hidden_framebuffer) <
        ((vga_screen.height - 8) * vga_screen.width / 2)) {
      uint8_t *sprite_row_ptr = (uint8_t *)&image_start[spr->stride * y];
      for (int x = 0; x < spr->stride - 1; x++) {
        uint8_t *sprite_pixel_ptr = sprite_row_ptr + x * size_stride;
        int pix = (x * VGA_GROUP_PIXELS) + spr_x; /* start pixel */
        uint32_t packed = *(uint32_t *)sprite_pixel_ptr;
        if (packed == 0xCCCCCCCC) continue; /* skip fully transparent group */
        uint8_t *pal_ptr = (uint8_t *)&packed;
        unsigned pos0 = (unsigned)pix & 0xF; /* first pixel position */
        uint block_ofs = (unsigned)pix >> 4;
        uint8_t *block = line + (block_ofs * (VGA_NUM_BITPLANES * 2));
        if (pos0 <= 12) {
          /* Entirely inside one block */
          uint64_t cur = *(uint64_t *)block;
          uint64_t clear_mask = 0, set_mask = 0;
          for (int p = 0; p < 4; ++p) {
            uint8_t palv = pal_ptr[p];
            if (palv == 0xCC) continue;
            int rel = pix + p - spr_x;
            if (rel >= width) break;
            unsigned pos = (pos0 + (unsigned)p) & 0xF;
            clear_mask |= pixel_masks_flat[(0xF << 4) | pos];
            uint8_t idx = rgb2index[palv & 0x3F];
            set_mask |= pixel_masks_flat[(idx << 4) | pos];
          }
          cur = (cur & ~clear_mask) | set_mask;
          ((uint32_t *)block)[0] = (uint32_t)cur;
          ((uint32_t *)block)[1] = (uint32_t)(cur >> 32);
        } else {
          /* Cross-block split */
          uint8_t *block_next = block + (VGA_NUM_BITPLANES * 2);
          uint64_t cur0 = *(uint64_t *)block;
          uint64_t cur1 = *(uint64_t *)block_next;
          uint64_t clear0 = 0, set0 = 0, clear1 = 0, set1 = 0;
          unsigned first_count = 16u - pos0; /* pixels in first block (1..3) */
          for (unsigned p = 0; p < first_count; ++p) {
            uint8_t palv = pal_ptr[p];
            if (palv == 0xCC) continue;
            int rel = pix + (int)p - spr_x;
            if (rel >= width) break;
            unsigned pos = (pos0 + p) & 0xF;
            clear0 |= pixel_masks_flat[(0xF << 4) | pos];
            uint8_t idx = rgb2index[palv & 0x3F];
            set0 |= pixel_masks_flat[(idx << 4) | pos];
          }
          for (unsigned p = first_count; p < 4u; ++p) {
            uint8_t palv = pal_ptr[p];
            if (palv == 0xCC) continue;
            int rel = pix + (int)p - spr_x;
            if (rel >= width) break;
            unsigned pos = (unsigned)(p - first_count); /* next block start */
            clear1 |= pixel_masks_flat[(0xF << 4) | pos];
            uint8_t idx = rgb2index[palv & 0x3F];
            set1 |= pixel_masks_flat[(idx << 4) | pos];
          }
          cur0 = (cur0 & ~clear0) | set0;
          cur1 = (cur1 & ~clear1) | set1;
          ((uint32_t *)block)[0] = (uint32_t)cur0;
          ((uint32_t *)block)[1] = (uint32_t)(cur0 >> 32);
          ((uint32_t *)block_next)[0] = (uint32_t)cur1;
          ((uint32_t *)block_next)[1] = (uint32_t)(cur1 >> 32);
        }
      }
    }
  }
}

/* Opaque path (assumes no 0xCC transparent pixels present) */
void __not_in_flash_func(draw_sprite_opaque)(const struct SPRITE *spr,
                                             int spr_x, int spr_y) {
  const unsigned int *image_start = spr->data;
  int height = spr->height;
  if (spr_y < 0) {
    image_start += spr->stride * (-spr_y);
    height += spr_y;
    spr_y = 0;
  }
  if (height > (vga_screen.height - 8) - spr_y)
    height = (vga_screen.height - 8) - spr_y;
  if (height <= 0) return;
  int width = spr->width;
  if (spr_x < 0) {
    image_start += (-spr_x) / 4;
    width += spr_x;
    spr_x = ((unsigned int)spr_x) % 4;
  }
  if (width > vga_screen.width - spr_x) width = vga_screen.width - spr_x;
  if (width <= 0) return;
  int row_bytes = vga_screen.width / (VGA_BLOCK_PIXELS / 2);
  int size_stride = sizeof(*image_start);
  for (int y = 0; y < height; y++) {
    uint8_t *line =
        (uint8_t *)&vga_screen.hidden_framebuffer[(y + spr_y) * row_bytes];
    if ((line - (uint8_t *)vga_screen.hidden_framebuffer) <
        ((vga_screen.height - 8) * vga_screen.width / 2)) {
      uint8_t *sprite_row_ptr = (uint8_t *)&image_start[spr->stride * y];
      for (int x = 0; x < spr->stride - 1; x++) {
        uint8_t *sprite_pixel_ptr = sprite_row_ptr + x * size_stride;
        int pix = (x * VGA_GROUP_PIXELS) + spr_x; /* start pixel of group */
        if (pix - spr_x >= width) break;          /* beyond clipped width */
        uint32_t packed = *(uint32_t *)sprite_pixel_ptr;
        uint8_t *pal_ptr = (uint8_t *)&packed;
        unsigned pos0 = (unsigned)pix & 0xF;
        uint block_start = (unsigned)pix >> 4;
        uint8_t *block = line + (block_start * (VGA_NUM_BITPLANES * 2));
        if (pos0 <= 12) {
          /* Fast single-block path: accumulate set bits only */
          uint64_t set_mask = 0;
          for (int p = 0; p < VGA_GROUP_PIXELS; ++p) {
            int abs_pix = pix + p;
            if (abs_pix - spr_x >= width) break; /* tail clip */
            unsigned pos = (pos0 + (unsigned)p) & 0xF;
            uint8_t idx = rgb2index[pal_ptr[p] & 0x3F];
            set_mask |= pixel_masks_flat[(idx << 4) | pos];
          }
          if (set_mask) {
            uint64_t cur = *(uint64_t *)block;
            cur |= set_mask; /* overwrite by OR (opaque assumption) */
            ((uint32_t *)block)[0] = (uint32_t)cur;
            ((uint32_t *)block)[1] = (uint32_t)(cur >> 32);
          }
        } else {
          /* Crosses into next block */
          uint8_t *block_next = block + (VGA_NUM_BITPLANES * 2);
          uint64_t set0 = 0, set1 = 0;
          unsigned first_count = 16u - pos0; /* pixels in first block */
          for (unsigned p = 0; p < first_count; ++p) {
            int abs_pix = pix + (int)p;
            if (abs_pix - spr_x >= width) break;
            unsigned pos = (pos0 + p) & 0xF;
            uint8_t idx = rgb2index[pal_ptr[p] & 0x3F];
            set0 |= pixel_masks_flat[(idx << 4) | pos];
          }
          for (unsigned p = first_count; p < VGA_GROUP_PIXELS; ++p) {
            int abs_pix = pix + (int)p;
            if (abs_pix - spr_x >= width) break;
            unsigned pos = (unsigned)(p - first_count); /* new block pos */
            uint8_t idx = rgb2index[pal_ptr[p] & 0x3F];
            set1 |= pixel_masks_flat[(idx << 4) | pos];
          }
          if (set0) {
            uint64_t cur0 = *(uint64_t *)block;
            cur0 |= set0;
            ((uint32_t *)block)[0] = (uint32_t)cur0;
            ((uint32_t *)block)[1] = (uint32_t)(cur0 >> 32);
          }
          if (set1) {
            uint64_t cur1 = *(uint64_t *)block_next;
            cur1 |= set1;
            ((uint32_t *)block_next)[0] = (uint32_t)cur1;
            ((uint32_t *)block_next)[1] = (uint32_t)(cur1 >> 32);
          }
        }
      }
    }
  }
}

void __not_in_flash_func(draw_tile)(const struct SPRITE *__restrict spr,
                                    int spr_x, int spr_y) {
  const unsigned int *image_start = spr->data;
  int width = spr->width;
  int height = spr->height;

  const int drawable_height = vga_screen.height - VGA_STATUS_BAR_OFFSET;

  /* Trivial reject if completely off-screen before clipping */
  if (spr_x >= vga_screen.width || spr_y >= drawable_height) return;
  if (spr_x + width <= 0 || spr_y + height <= 0) return;

  /* Vertical clipping */
  if (spr_y < 0) {
    int skip_rows = -spr_y;
    image_start += spr->stride * skip_rows;
    height -= skip_rows;
    spr_y = 0;
  }
  if (height > drawable_height - spr_y) height = drawable_height - spr_y;
  if (height <= 0) return;

  /* Horizontal clipping */
  unsigned shift = 0;  // NEW: carry leftover pixels
  if (spr_x < 0) {
    int skip_pixels = -spr_x;
    image_start +=
        (unsigned)skip_pixels / VGA_GROUP_PIXELS; /* advance whole words */
    width -= skip_pixels;
    shift = (unsigned)skip_pixels &
            (VGA_GROUP_PIXELS - 1); /* NEW: leftover within word */
    spr_x = 0; /* NEW: we’ve consumed the negative offset */
  }
  if (width > vga_screen.width - spr_x) width = vga_screen.width - spr_x;
  if (width <= 0) return;

  const int row_bytes =
      vga_screen.width / (VGA_BLOCK_PIXELS / 2); /* 16 px -> 8 bytes */
  const unsigned int *__restrict row_src_base =
      image_start; /* starting word pointer */
  row_src_base =
      (const unsigned int *__restrict)__builtin_assume_aligned(row_src_base, 4);

  // NEW: number of 32-bit words actually needed to cover 'width'
  const unsigned words_per_row =
      (unsigned)((width + (VGA_GROUP_PIXELS - 1)) / VGA_GROUP_PIXELS);

  for (unsigned row = 0; row < (unsigned)height; ++row) {
    uint8_t *line =
        (uint8_t *)&vga_screen.hidden_framebuffer[(spr_y + row) * row_bytes];

    const uint32_t *wp =
        (const uint32_t *)(row_src_base + (size_t)row * spr->stride);

    unsigned pix =
        (unsigned)spr_x + shift; /* include leftover shift from left-clip */
    const unsigned block_mask = VGA_BLOCK_PIXELS - 1u;
    const unsigned block_byte_shift = 3; /* (VGA_NUM_BITPLANES*2)==8 bytes */

    // NEW: counted loop over only the visible words (keeps your unroll-by-2)
    unsigned i = 0;
    while (i < words_per_row) {
      /* First iteration */
      uint32_t packed0 = *wp & VGA_RGB6_PACK_MASK;
      uint8_t *pal0 = (uint8_t *)&packed0;
      uint8_t idx0a = rgb2index[pal0[0]];
      uint8_t idx1a = rgb2index[pal0[1]];
      uint8_t idx2a = rgb2index[pal0[2]];
      uint8_t idx3a = rgb2index[pal0[3]];
      unsigned pos0a = pix & block_mask;
      unsigned pos1a = (pix + 1u) & block_mask;
      unsigned pos2a = (pix + 2u) & block_mask;
      unsigned pos3a = (pix + 3u) & block_mask;
      uint64_t mask_a = pixel_masks_flat[(idx0a << 4) | pos0a] |
                        pixel_masks_flat[(idx1a << 4) | pos1a] |
                        pixel_masks_flat[(idx2a << 4) | pos2a] |
                        pixel_masks_flat[(idx3a << 4) | pos3a];
      uint64_t *block64_a =
          (uint64_t *)(line + (((pix >> 4) << block_byte_shift)));
      if (likely(pos0a == 0)) {
        *block64_a = mask_a; /* overwrite whole 16px block */
      } else {
        *block64_a |= mask_a; /* merge into existing block */
      }
      ++wp;
      pix += VGA_GROUP_PIXELS;
      ++i;

      /* Second unrolled iteration */
      if (i >= words_per_row) break;
      uint32_t packed1 = *wp & VGA_RGB6_PACK_MASK;
      uint8_t *pal1 = (uint8_t *)&packed1;
      uint8_t idx0b = rgb2index[pal1[0]];
      uint8_t idx1b = rgb2index[pal1[1]];
      uint8_t idx2b = rgb2index[pal1[2]];
      uint8_t idx3b = rgb2index[pal1[3]];
      unsigned pos0b = pix & block_mask;
      unsigned pos1b = (pix + 1u) & block_mask;
      unsigned pos2b = (pix + 2u) & block_mask;
      unsigned pos3b = (pix + 3u) & block_mask;
      uint64_t mask_b = pixel_masks_flat[(idx0b << 4) | pos0b] |
                        pixel_masks_flat[(idx1b << 4) | pos1b] |
                        pixel_masks_flat[(idx2b << 4) | pos2b] |
                        pixel_masks_flat[(idx3b << 4) | pos3b];
      uint64_t *block64_b =
          (uint64_t *)(line + (((pix >> 4) << block_byte_shift)));
      if (pos0b == 0) {
        *block64_b = mask_b;
      } else {
        *block64_b |= mask_b;
      }
      ++wp;
      pix += VGA_GROUP_PIXELS;
      ++i;
    }
  }
}