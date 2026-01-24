/**
 * File: emul.c
 * Author: Diego Parrilla Santamaría
 * Date: February 2025
 * Copyright: 2025 - GOODDATA LABS
 * Description: Template code for the core emulation
 */

#include "emul.h"

#include <stdlib.h>
#include <string.h>

#include "data/font6x8.h"
#include "data/loserboy.h"
#include "data/tiles.h"
#include "memfunc.h"
#include "target_firmware.h"

#define USE_DMA_BG_COPY 1

#define CUSTOM_FRAMEBUFFER_INDEX 0x5fc
#define CUSTOM_DISPLAY_COMMAND 0x5f8
#define REMOTE_ROM4_ADDRESS 0xFA0000
#define REMOTE_ROM3_ADDRESS 0xFB0000
#define REMOTE_ATARI_ST_SCREEN_A_ADDRESS_512KB 0x70000
#define REMOTE_ATARI_ST_SCREEN_B_ADDRESS_512KB 0x78000
#define REMOTE_ATARI_ST_SCREEN_ADDRESS_1MB 0xF8000

struct SPRITE bg_tiles[img_tiles_num_spr];
struct SPRITE char_frames[img_loserboy_num_spr];
struct CHARACTER characters[NUM_SPRITES];
static struct SPRITE *bg_tile_ptrs[15];

static semaphore_t draw_sem;
static semaphore_t start_demo_sem;
static volatile bool startBooster =
    false;  // Flag to indicate if the booster should start

static uint32_t memorySharedAddress = 0;
static uint32_t memoryRandomTokenAddress = 0;
static uint32_t memoryRandomTokenSeedAddress = 0;

static uint32_t displayCommandAddress = 0;
static unsigned char *framebuffer = NULL;
static int color_ring = 15;
static int sprite_count = 0;
static unsigned int last_sprite_increment = 0;
static int active_talker = -1;
static unsigned int *img_tiles_data_ram = NULL;
static unsigned int *img_loserboy_data_ram = NULL;
static uint8_t *font6x8_data_ram = NULL;
static uint32_t *font6x8_data_words = NULL;
static struct VGA_FONT font6x8_ram;
static unsigned int *bg_framebuffer_ram = NULL;
static size_t bg_framebuffer_bytes = 0;
static size_t bg_copy_bytes_full = 0;
static size_t bg_copy_bytes_trim = 0;
static bool bg_full_copy_done = false;

static inline int count_fps(void) {
  static int last_fps;
  static int frame_count;
  static unsigned last_ms;

  unsigned int cur_ms = to_ms_since_boot(get_absolute_time());
  if (cur_ms / 1000 != last_ms / 1000) {
    last_fps = frame_count;
    frame_count = 0;
  }
  frame_count++;
  last_ms = cur_ms;
  return last_fps;
}

static void init_sprites(void) {
  if (img_tiles_data_ram == NULL || img_loserboy_data_ram == NULL) {
    DPRINTF("ERROR: sprite data not initialized in RAM\n");
    return;
  }
  for (int i = 0; i < count_of(bg_tiles); i++) {
    struct SPRITE *spr = &bg_tiles[i];
    spr->width = img_tiles_width;
    spr->height = img_tiles_height;
    spr->stride = img_tiles_stride;
    spr->data = (unsigned int *)&img_tiles_data_ram[i * img_tiles_stride *
                                                    img_tiles_height];
  }

  for (int i = 0; i < count_of(char_frames); i++) {
    struct SPRITE *spr = &char_frames[i];
    spr->width = img_loserboy_width;
    spr->height = img_loserboy_height;
    spr->stride = img_loserboy_stride;
    spr->data = (unsigned int *)&img_loserboy_data_ram[i * img_loserboy_stride *
                                                       img_loserboy_height];
  }

  for (int i = 0; i < NUM_SPRITES; i++) {
    struct CHARACTER *ch = &characters[i];
    ch->x = rand() % (vga_screen.width - img_loserboy_width);
    ch->y = rand() % (vga_screen.height - img_loserboy_height);
    ch->dx = (1 + rand() % 3) * ((rand() & 1) ? -1 : 1);
    ch->dy = (1 + rand() % 2) * ((rand() & 1) ? -1 : 1);
    ch->frame = i + i * loserboy_walk_frame_delay;
    ch->sprite = NULL;
    ch->message_index = -1;
    ch->message_frame = -1;
  }
}

static void __not_in_flash_func(move_character)(struct CHARACTER *ch) {
  int ch_index = (int)(ch - characters);
  if (ch->message_frame-- < 0) {
    ch->message_index = -1;
    ch->message_frame = 600 + rand() % 1200;
    if (active_talker == ch_index) {
      active_talker = -1;
    }
  } else if (ch->message_frame == 180) {
    ch->message_index = rand() % count_of(loserboy_messages);
    active_talker = ch_index;
  }

  if (ch->message_frame > 1500) {
    ch->sprite = &char_frames[loserboy_stand_frame +
                              ((ch->dx < 0) ? loserboy_mirror_frame_start : 0)];
  } else {
    ch->x += ch->dx;
    if (ch->x < -ch->sprite->width / 2) ch->dx = 1 + rand() % 3;
    if (ch->x >= vga_screen.width - ch->sprite->width / 2)
      ch->dx = -(1 + rand() % 3);

    ch->y += ch->dy;
    if (ch->y < -ch->sprite->height / 2) ch->dy = 1 + rand() % 2;
    if (ch->y >= (vga_screen.height - 8) - ch->sprite->height / 2)
      ch->dy = -(1 + rand() % 2);

    ch->frame++;
    if (ch->frame / loserboy_walk_frame_delay >=
        count_of(loserboy_walk_cycle)) {
      ch->frame = 0;
    }
    int frame_num = loserboy_walk_cycle[ch->frame / loserboy_walk_frame_delay];
    ch->sprite = &char_frames[frame_num +
                              ((ch->dx < 0) ? loserboy_mirror_frame_start : 0)];
  }
}

// Interrupt handler for DMA completion
void __not_in_flash_func(emul_dma_irq_handler_lookup)(void) {
  // Which channels triggered IRQ1?
  uint32_t m = dma_hw->ints1;

  // Handle channel 2 only
  if (m & (1u << 2)) {
    bool rom3_gpio = (1ul << ROM3_GPIO) & sio_hw->gpio_in;
    // Snapshot anything you need from the DMA channel
    uint32_t addr = dma_hw->ch[2].al3_read_addr_trig;

    // Ack just channel 2 (write-1-to-clear)
    dma_hw->ints1 = (1u << 2);

    // Check ROM3 signal (bit 16)
    // We expect that the ROM3 signal is not set very often, so this should help
    // the compilar to run faster
    if (!rom3_gpio) {
      // if (__builtin_expect(addr & 0x00010000, 0)) {
      // DPRINTF("Address: 0x%08X\n", addr);
      // Invert highest bit of low word to get 16-bit address
      uint16_t addr_lsb = (uint16_t)(addr ^ ADDRESS_HIGH_BIT);

      switch (addr_lsb) {
        case 0xDCBA:  // draw tick
          sem_release(&draw_sem);
          break;
        case 0xE1A8:  // start demo
          sem_release(&start_demo_sem);
          break;
        case 0xABCD:  // ESC key -> start booster
          startBooster = true;
          sem_release(&draw_sem);
          DPRINTF("Booster started\n");
          break;
        default:
          break;  // no action
      }
    }
  }
}

// Setter function for display command address
void setDisplayCommandAddress(uint32_t address) {
  displayCommandAddress = address;
}

void emul_preinit() {
  setDisplayCommandAddress((unsigned int)&__rom_in_ram_start__ +
                           CUSTOM_DISPLAY_COMMAND);

  // Memory shared address
  memorySharedAddress = (unsigned int)&__rom_in_ram_start__;
  memoryRandomTokenAddress = memorySharedAddress + TERM_RANDOM_TOKEN_OFFSET;
  memoryRandomTokenSeedAddress =
      memorySharedAddress + TERM_RANDON_TOKEN_SEED_OFFSET;
  SET_SHARED_VAR(TERM_HARDWARE_TYPE, 0, memorySharedAddress,
                 TERM_SHARED_VARIABLES_OFFSET);  // Clean the hardware type
  SET_SHARED_VAR(TERM_HARDWARE_VERSION, 0, memorySharedAddress,
                 TERM_SHARED_VARIABLES_OFFSET);  // Clean the hardware version
}

// Getter function for display command address
uint32_t emul_getCommandAddress() { return displayCommandAddress; }

void __not_in_flash_func(emul_start)() {
  // The anatomy of an app or microfirmware is as follows:
  // - The driver code running in the remote device (the computer)
  // - the driver code running in the host device (the rp2040/rp2350)
  //
  // The driver code running in the remote device is responsible for:
  // 1. Perform the emulation of the device (ex: a ROM cartridge)
  // 2. Handle the communication with the host device
  // 3. Handle the configuration of the driver (ex: the ROM file to load)
  // 4. Handle the communication with the user (ex: the terminal)
  //
  // The driver code running in the host device is responsible for:
  // 1. Handle the communication with the remote device
  // 2. Handle the configuration of the driver (ex: the ROM file to load)
  // 3. Handle the communication with the user (ex: the terminal)
  //
  // Hence, we effectively have two drivers running in two different devices
  // with different architectures and capabilities.
  //
  // Please read the documentation to learn to use the communication protocol
  // between the two devices in the tprotocol.h file.
  //

  // 1. Check if the host device must be initialized to perform the emulation
  //    of the device, or start in setup/configuration mode

  // Not needed in this demo

  // 2. Initialiaze the normal operation of the app, unless the configuration
  // option says to start the config app Or a SELECT button is (or was)
  // pressed to start the configuration section of the app

  // In this example, the flow will always start the configuration app first
  // The ROM Emulator app for example will check here if the start directly
  // in emulation mode is needed or not
  emul_preinit();

  // 3. If we are here, it means the app is not in emulation mode, but in
  // setup/configuration mode

  // As a rule of thumb, the remote device (the computer) driver code must
  // be copied to the RAM of the host device where the emulation will take
  // place.
  // The code is stored as an array in the target_firmware.h file
  //
  // Copy the terminal firmware to RAM
  COPY_FIRMWARE_TO_RAM((uint16_t *)target_firmware, target_firmware_length * 2);

  // Initialize the terminal emulator PIO programs
  // The communication between the remote (target) computer and the RP2040 is
  // done using a command protocol over the cartridge bus
  // term_dma_irq_handler_lookup is the implementation of the terminal
  // emulator using the command protocol. Hence, if you want to implement your
  // own app or microfirmware, you should implement your own command handler
  // using this protocol.
  SEND_COMMAND_TO_DISPLAY(DISPLAY_COMMAND_NOP);
  init_romemul(NULL, emul_dma_irq_handler_lookup, false);
  sem_init(&draw_sem, 0, 1);
  sem_init(&start_demo_sem, 0, 1);

  // 4. During the setup/configuration mode, the driver code must interact
  // with the user to configure the device. To simplify the process, the
  // terminal emulator is used to interact with the user.
  // The terminal emulator is a simple text-based interface that allows the
  // user to configure the device using text commands.
  // If you want to use a custom app in the remote computer, you can do it.
  // But it's easier to debug and code in the rp2040

  // 8. Configure the SELECT button
  // Short press: reset the device and restart the app
  // Long press: reset the device and erase the flash.
  select_configure();
  select_coreWaitPush(
      reset_device,
      reset_deviceAndEraseFlash);  // Wait for the SELECT button to be pushed

  DPRINTF("SELECT button configured\n");

  // Initialize the display

  // DPRINTF("Drawing color index for image tiles\n");
  // draw_show_color_index(img_tiles_data,
  //                       sizeof(img_tiles_data) /
  //                       sizeof(img_tiles_data[0]));
  // DPRINTF("Drawing color index for image tiles\n");
  // draw_show_color_index(img_loserboy_data_flash,
  // sizeof(img_loserboy_data_flash) /
  //                                              sizeof(img_loserboy_data_flash[0]));

  // xip_ctrl_hw->ctrl &= ~XIP_CTRL_EN_BITS;

  uint32_t local_fb_a = (unsigned int)&__rom_in_ram_start__ + 0x10000 - 32000;
  uint32_t local_fb_b = local_fb_a - 32000;
  uint32_t remote_fb_a = REMOTE_ROM3_ADDRESS - 64000;
  uint32_t remote_fb_b = REMOTE_ROM3_ADDRESS - 32000;
  uint32_t local_copycode_a = (unsigned int)&__rom_in_ram_start__ + 0x600;
  uint32_t local_copycode_b = local_copycode_a + 0x2000;
  uint32_t local_copycode_size = 0x1F48;

  // We init the VGA framebuffers
  if (vga_init(&vga_mode_320x200, local_fb_a, local_fb_b) < 0) {
    DPRINTF("ERROR initializing VGA\n");
    while (1) {
      sleep_ms(SLEEP_LOOP_MS);
    }
  }

  DPRINTF("VGA initialized successfully\n");

  // We are going to allocate temporaly the code to copy the framebuffers in
  // the framebuffers
  vga_copy_to_display(remote_fb_a, (void *)local_copycode_a,
                      REMOTE_ATARI_ST_SCREEN_A_ADDRESS_512KB);
  vga_copy_to_display(remote_fb_b, (void *)local_copycode_b,
                      REMOTE_ATARI_ST_SCREEN_B_ADDRESS_512KB);
  DPRINTF("VGA framebuffers copied to display\n");
  DPRINTF("Waiting for the demo to start...\n");

  sem_acquire_blocking(&start_demo_sem);

  DPRINTF("Demo started!\n");

  DPRINTF("Initializing pixel masks\n");
  init_pixel_masks();
  DPRINTF("Pixel masks initialized\n");
  img_tiles_data_ram = (unsigned int *)malloc(sizeof(img_tiles_data_flash));
  img_loserboy_data_ram =
      (unsigned int *)malloc(sizeof(img_loserboy_data_flash));
  size_t font_words = (sizeof(font6x8_data_flash) + 3u) / 4u;
  font6x8_data_words = (uint32_t *)malloc(font_words * sizeof(uint32_t));
  font6x8_data_ram = (uint8_t *)font6x8_data_words;
  if (img_tiles_data_ram == NULL || img_loserboy_data_ram == NULL ||
      font6x8_data_ram == NULL) {
    DPRINTF("ERROR: failed to allocate RAM for asset data\n");
    while (1) {
      sleep_ms(SLEEP_LOOP_MS);
    }
  }
  memcpy(img_tiles_data_ram, img_tiles_data_flash,
         sizeof(img_tiles_data_flash));
  memcpy(img_loserboy_data_ram, img_loserboy_data_flash,
         sizeof(img_loserboy_data_flash));
  memcpy(font6x8_data_ram, font6x8_data_flash, sizeof(font6x8_data_flash));
  font6x8_ram = font6x8;
  font6x8_ram.data = font6x8_data_ram;
  font_set_font(&font6x8_ram);
  DPRINTF("Font set to 6x8\n");
  font_set_color(15);
  DPRINTF("Font color set to 15\n");
  init_sprites();
  DPRINTF("Sprites initialized\n");
  for (int i = 0; i < count_of(bg_tile_ptrs); i++) {
    bg_tile_ptrs[i] = &bg_tiles[bg_map[i]];
  }
  bg_framebuffer_bytes =
      vga_screen.width / (8 / vga_screen.color_bits) * vga_screen.height;
  bg_copy_bytes_full = bg_framebuffer_bytes;
  size_t row_bytes = vga_screen.width / (8 / vga_screen.color_bits);
  bg_copy_bytes_trim = bg_framebuffer_bytes - (row_bytes * 8u);
  size_t bg_words = (bg_framebuffer_bytes + 3u) / 4u;
  bg_framebuffer_ram = (unsigned int *)malloc(bg_words * sizeof(unsigned int));
  if (bg_framebuffer_ram == NULL) {
    DPRINTF("ERROR: failed to allocate RAM for background buffer\n");
    while (1) {
      sleep_ms(SLEEP_LOOP_MS);
    }
  }
  unsigned int *saved_hidden = vga_screen.hidden_framebuffer;
  vga_screen.hidden_framebuffer = bg_framebuffer_ram;
  vga_clear_screen();
  {
    const int tile_w = img_tiles_width;
    const int tile_h = img_tiles_height;
    struct SPRITE **tile_ptr = bg_tile_ptrs;
    for (int ty = 0; ty < 3; ty++) {
      int y = ty * tile_h;
      for (int tx = 0; tx < 5; tx++) {
        struct SPRITE *tile = *tile_ptr++;
        draw_tile(tile, tx * tile_w, y);
      }
    }
    font_align(FONT_ALIGN_LEFT);
    font_set_border(false, 8);
    font_set_color(15);
    font_move(0, 192);
    font_printf(" Press any key to boot GEM. ");
    font_printf("ESC to return to Booster.");
  }
  vga_screen.hidden_framebuffer = saved_hidden;

  // draw keyboard shortcuts
  font_align(FONT_ALIGN_LEFT);
  font_set_border(false, 8);
  font_set_color(15);

  vga_swap_framebuffers();
  font_move(0, 192);
  font_printf(" Press any key to boot GEM. ");
  font_printf("ESC to return to Booster.");

  // We do it twice because it does not change in the framebuffer
  vga_swap_framebuffers();
  font_move(0, 192);
  font_printf(" Press any key to boot GEM. ");
  font_printf("ESC to return to Booster.");

  // 9. Start the main loop
  // The main loop is the core of the app. It is responsible for running the
  // app, handling the user input, and performing the tasks of the app.
  // The main loop runs until the user decides to exit.
  // For testing purposes, this app only shows commands to manage the settings
  DPRINTF("Start the app loop here\n");
  int font_align_state = FONT_ALIGN_LEFT;
  font_set_border(true, 8);

  while (1) {
    sem_acquire_blocking(&draw_sem);
    if (startBooster) break;

    unsigned int start_ms = to_ms_since_boot(get_absolute_time());
    for (int i = 0; i < sprite_count; i++) {
      move_character(&characters[i]);
    }

    // draw background
    size_t copy_bytes =
        bg_full_copy_done ? bg_copy_bytes_trim : bg_copy_bytes_full;
#if USE_DMA_BG_COPY
    COPY_32BIT_DMA(vga_screen.hidden_framebuffer, bg_framebuffer_ram,
                   copy_bytes);
#else
    memcpy(vga_screen.hidden_framebuffer, bg_framebuffer_ram, copy_bytes);
#endif
    bg_full_copy_done = true;
    // draw sprites
    for (int i = 0; i < sprite_count; i++) {
      struct CHARACTER *ch = &characters[i];
      draw_sprite(ch->sprite, ch->x, ch->y, true);
    }
    if (active_talker >= sprite_count) {
      active_talker = -1;
    }
    if (active_talker >= 0) {
      struct CHARACTER *ch = &characters[active_talker];
      if (ch->message_index >= 0) {
        int msg_x = ch->x + ch->sprite->width / 2;
        int msg_y = ch->y - 10;
        int msg_index = ch->message_index;
        if (font_align_state != FONT_ALIGN_CENTER) {
          font_align(FONT_ALIGN_CENTER);
          font_align_state = FONT_ALIGN_CENTER;
        }
        font_move(msg_x, msg_y);
        font_print(loserboy_messages[msg_index]);
      }
    }

    // draw fps counter
    if (font_align_state != FONT_ALIGN_LEFT) {
      font_align(FONT_ALIGN_LEFT);
      font_align_state = FONT_ALIGN_LEFT;
    }
    int fps = count_fps();
    font_move(0, 0);
    font_printf("%04d fps", fps);

    font_move(0, 8);
    font_printf("Sprites: %d", sprite_count);

    // font_move(0, 16);
    // font_printf("Millisecs: %d", end_ms - start_ms);

    vga_swap_framebuffers();

    unsigned int end_ms = to_ms_since_boot(get_absolute_time());
    if (end_ms - last_sprite_increment > NEW_SPRITE_INTERVAL_MS) {
      last_sprite_increment = end_ms;
      // Only increment the number of sprites on screen if the frame duration is
      // in the VBLANK
      if (end_ms - start_ms < 19) {
        sprite_count = (sprite_count + 1) % NUM_SPRITES;
      }
    }

    WRITE_AND_SWAP_LONGWORD((unsigned int)&__rom_in_ram_start__,
                            CUSTOM_FRAMEBUFFER_INDEX,
                            (uint32_t)vga_screen.current_framebuffer_id);
  }

  // 10. Send RESET computer command
  // Ok, so we are done with the setup but we want to reset the computer to
  // reboot in the same microfirmware app or start the booster app

  DPRINTF("Resetting the computer...\n");
  select_setResetCallback(NULL);      // Disable the reset callback
  select_setLongResetCallback(NULL);  // Disable the long reset callback
  select_coreWaitPushDisable();       // Disable the SELECT button
  sleep_ms(SLEEP_LOOP_MS);
  // We must reset the computer
  SEND_COMMAND_TO_DISPLAY(DISPLAY_COMMAND_RESET);
  sleep_ms(SLEEP_LOOP_MS);

  // Jump to the booster app
  DPRINTF("Jumping to the booster app...\n");
  reset_jump_to_booster();

  while (1) {
    // Wait for the computer to start
    sleep_ms(SLEEP_LOOP_MS);
  }
}
