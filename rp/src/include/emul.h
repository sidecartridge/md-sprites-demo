/**
 * File: emul.h
 * Author: Diego Parrilla Santamaría
 * Date: January 20205
 * Copyright: 2025 - GOODDATA LABS SL
 * Description: Header for the ROM emulator core and setup features
 */

#ifndef EMUL_H
#define EMUL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "aconfig.h"
#include "constants.h"
#include "debug.h"
#include "memfunc.h"
#include "pico/sem.h"  // semaphore API
#include "pico/stdlib.h"
#include "reset.h"
#include "romemul.h"
#include "select.h"
#include "vga/draw.h"
#include "vga/font.h"
#include "vga/vga.h"

#define NUM_SPRITES 127              // number of sprites to draw
#define NEW_SPRITE_INTERVAL_MS 3000  // 3 seconds

#define ADDRESS_HIGH_BIT 0x8000  // High bit of the address

#ifndef ROM3_GPIO
#define ROM3_GPIO 26
#endif

#ifndef ROM4_GPIO
#define ROM4_GPIO 22
#endif

// Use the highest 4K of the shared memory for the terminal commands
#define TERM_RANDOM_TOKEN_OFFSET \
  0xF000  // Random token offset in the shared memory
#define TERM_RANDON_TOKEN_SEED_OFFSET \
  (TERM_RANDOM_TOKEN_OFFSET +         \
   4)  // Random token seed offset in the shared memory: 0xF004

// Size of the shared variables of the shared functions
#define SHARED_VARIABLE_SHARED_FUNCTIONS_SIZE \
  16  // Leave a gap for the shared variables of the shared functions

// The shared variables are located in the + 0x200 offset
#define TERM_SHARED_VARIABLES_OFFSET        \
  (TERM_RANDOM_TOKEN_OFFSET +               \
   (SHARED_VARIABLE_SHARED_FUNCTIONS_SIZE * \
    4))  // Shared variables offset in the shared memory: 0xF000 + (16 * 4)

// Shared variables for common use. Must be set in the init function
#define TERM_HARDWARE_TYPE (0)     // Hardware type. 0xF200
#define TERM_HARDWARE_VERSION (1)  // Hardware version.  0xF204

// App commands for the terminal
#define APP_TERMINAL 0x00  // The terminal app

// App terminal commands
#define APP_BOOSTER_START 0x00  // Launch the booster app

#define DISPLAY_COMMAND_BOOSTER 0x3  // Enter booster mode

#define TERM_PARAMETERS_MAX_SIZE 20  // Maximum size of the parameters

#define SLEEP_LOOP_MS 100

// Commands sent to the active loop in the display terminal application
#define DISPLAY_COMMAND_NOP 0x0       // Do nothing, clean the command buffer
#define DISPLAY_COMMAND_RESET 0x1     // Reset the computer
#define DISPLAY_COMMAND_CONTINUE 0x2  // Continue the boot process
#define DISPLAY_COMMAND_TERMINAL \
  0x3                              //  Terminal. Not used from RP to Computer.
#define DISPLAY_COMMAND_START 0x4  // Continue boot process and emulation

// Display buffer offset
#define DISPLAY_BUFFER_OFFSET 0x8000

// Commands offset. BUFFER_OFFSET + ADDRESS_OFFSET
#define DISPLAY_COMMAND_ADDRESS_OFFSET 8000

/**
 * @brief Sends a command to the display.
 *
 * @param command The command to be sent to the display.
 */
#define SEND_COMMAND_TO_DISPLAY(command)                           \
  do {                                                             \
    DPRINTF("Sending command: %08x\n", command);                   \
    WRITE_AND_SWAP_LONGWORD(emul_getCommandAddress(), 0, command); \
  } while (0)

/**
 * @brief Retrieves the display command address.
 *
 * Returns the command address used for sending instructions to the display
 * hardware. Use to send commands to the display terminal application.
 *
 * @return The display command address.
 */
uint32_t emul_getCommandAddress();

enum {
  APP_DIRECT = 0,       // Emulation
  APP_MODE_SETUP = 255  // Setup
};

#define APP_MODE_SETUP_STR "255"  // App mode setup string

struct CHARACTER {
  struct SPRITE *sprite;
  int message_index;
  int x;
  int y;
  int dx;
  int dy;
  int frame;
  int message_frame;
};

#define loserboy_stand_frame 10
#define loserboy_mirror_frame_start 11
#define loserboy_walk_frame_delay 4
static const unsigned int loserboy_walk_cycle[] = {
    5, 6, 7, 8, 9, 8, 7, 6, 5, 0, 1, 2, 3, 4, 3, 2, 1, 0,
};
static const unsigned char bg_map[20] = {
    0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0,
};

static const char *loserboy_messages[] = {
    "I'll get you!",     "Come back here!", "Ayeeeee!",
    "You can't escape!", "Take this!",
};

/**
 * @brief
 *
 * Launches the ROM emulator application. Initializes terminal interfaces,
 * configures network and storage systems, and loads the ROM data from SD or
 * network sources. Manages the main loop which includes firmware bypass,
 * user interaction and potential system resets.
 */
void emul_start();

#endif  // EMUL_H
