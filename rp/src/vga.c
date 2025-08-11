/**
 * vga_6bit.c
 *
 * Copyright (C) 2021 MoeFH
 * Released under the MIT License
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "vga/vga.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "debug.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/structs/bus_ctrl.h"
#include "pico/stdlib.h"
//                                        hpix, vpix, colorbits
const struct VGA_MODE vga_mode_320x200 = {320, 200, 4};

const static struct VGA_MODE *vga_mode = NULL;

static volatile uint frame_count;

struct VGA_SCREEN vga_screen;

inline void vga_clear_screen() {
  memset(vga_screen.hidden_framebuffer, 0,
         vga_screen.width / (8 / vga_screen.color_bits) * vga_screen.height);
}

int vga_init(const struct VGA_MODE *mode, uint32_t framebuffer_a,
             uint32_t framebuffer_b) {
  vga_mode = mode;
  vga_screen.width = vga_mode->h_pixels;
  vga_screen.height = vga_mode->v_pixels;
  vga_screen.color_bits = vga_mode->color_bits;
  // before: vga_screen.framebuffer = displayAddress;
  vga_screen.framebuffer_a = (unsigned int *)(uintptr_t)framebuffer_a;
  vga_screen.framebuffer_b = (unsigned int *)(uintptr_t)framebuffer_b;
  vga_screen.current_framebuffer = vga_screen.framebuffer_a;
  vga_screen.hidden_framebuffer = vga_screen.framebuffer_b;
  vga_screen.current_framebuffer_id = 0;
  vga_screen.hidden_framebuffer_id = 1;
  DPRINTF("VGA initialized: %dx%d, %d bpp\n", vga_screen.width,
          vga_screen.height, vga_screen.color_bits);
  DPRINTF("Current framebuffer address: %p\n", vga_screen.current_framebuffer);
  DPRINTF("Hidden framebuffer address: %p\n", vga_screen.hidden_framebuffer);
  DPRINTF("Current framebuffer ID: %d\n", vga_screen.current_framebuffer_id);
  DPRINTF("Hidden framebuffer ID: %d\n", vga_screen.hidden_framebuffer_id);
  vga_clear_screen();
  vga_swap_framebuffers();
  vga_clear_screen();
  vga_swap_framebuffers();
  return 0;
}

void vga_copy_to_display(uint32_t cartridge_fb, void *code_address,
                         uint32_t st_screen_address) {
  uint16_t *dst = (uint16_t *)code_address;
  int idx = 0;

  // --- Emit code to save A7 to $4C8 ---
  // MOVE.L A7, $000004C8
  *dst++ = 0x21CF;  // MOVE.L A7, (xxx).w
  *dst++ = 0x04c8;  // Address: $4c8.w

  while (idx < 8000) {
    // MOVEM.L $addr, D0-D7/A0-A7
    *dst++ = 0x4CF9;
    *dst++ = 0xFFFF;
    uint32_t src_addr = cartridge_fb + idx * 4;
    *dst++ = (uint16_t)(src_addr >> 16);
    *dst++ = (uint16_t)(src_addr & 0xFFFF);
#if defined(_DEBUG) && (_DEBUG != 0)
    if (idx == 0) {
      DPRINTF("$%p:    MOVEM.L $%04X, D0-D7/A0-A7\n", dst - 8, src_addr);
    }
#endif

    // MOVEM.L D0-D7/A0-A7, $addr
    *dst++ = 0x48F9;
    *dst++ = 0xFFFF;
    uint32_t dst_addr = st_screen_address + idx * 4;
    *dst++ = (uint16_t)(dst_addr >> 16);
    *dst++ = (uint16_t)(dst_addr & 0xFFFF);
#if defined(_DEBUG) && (_DEBUG != 0)
    if (idx == 0) {
      DPRINTF("$%p:    MOVEM.L D0-D7/A0-A7, $%04X\n", dst - 8, dst_addr);
    }
#endif

    idx += 16;
  }

  // --- Emit code to restore A7 from $4c8 ---
  *dst++ = 0x2E78;  // MOVE.L (xxx).w, A7
  *dst++ = 0x04c8;  // Address: $4c8.w

  *dst = 0x4E75;  // RTS

  DPRINTF("Code size: %x bytes\n", ((uint32_t)dst - (uint32_t)code_address));
  DPRINTF("RTS generated at %p\n",
          ((uint32_t)dst - (uint32_t)code_address + cartridge_fb));
}