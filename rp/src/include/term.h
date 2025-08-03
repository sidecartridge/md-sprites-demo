/**
 * File: term.h
 * Author: Diego Parrilla Santamaría
 * Date: January 20205
 * Copyright: 2025 - GOODDATA LABS SL
 * Description: Header for the terminal
 */

#ifndef TERM_H
#define TERM_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aconfig.h"
#include "constants.h"
#include "debug.h"
#include "display_term.h"
#include "hardware/dma.h"
#include "memfunc.h"
#include "reset.h"
#include "time.h"
#include "tprotocol.h"

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
#define APP_TERMINAL_START 0x00      // Enter terminal command
#define APP_TERMINAL_KEYSTROKE 0x01  // Keystroke command

#ifdef DISPLAY_ATARIST
// Terminal size for Atari ST
#define TERM_SCREEN_SIZE_X 40
#define TERM_SCREEN_SIZE_Y 24  // Leave last line for status
#define TERM_SCREEN_SIZE (TERM_SCREEN_SIZE_X * TERM_SCREEN_SIZE_Y)

#define TERM_DISPLAY_BYTES_PER_CHAR 8
#define TERM_DISPLAY_ROW_BYTES \
  (TERM_DISPLAY_BYTES_PER_CHAR * TERM_SCREEN_SIZE_X)
#endif

// Holds up to one line of user input (between '\n' or '\r')
#define TERM_PRINT_SETTINGS_BUFFER_SIZE 2048
#define TERM_INPUT_BUFFER_SIZE 256
#define TERM_ESC_BUFFLINE_SIZE 16
#define TERM_BOOL_INPUT_BUFF 8

#define TERM_ESC_CHAR 0x1B  // Escape character
#define TERM_POS_X 0x20     // Position X
#define TERM_POS_Y 0x20     // Position Y

#define TERM_KEYBOARD_KEY_START 0x20         // Start of the ASCII table
#define TERM_KEYBOARD_KEY_END 0x7E           // End of the ASCII table
#define TERM_KEYBOARD_KEY_MASK 0xFF          // Mask for the key
#define TERM_KEYBOARD_SHIFT_MASK 0xFF000000  // Mask for the shift key
#define TERM_KEYBOARD_SHIFT_SHIFT 24         // Shift for the shift key
#define TERM_KEYBOARD_SCAN_MASK 0xFF0000     // Mask for the scan code
#define TERM_KEYBOARD_SCAN_SHIFT 16          // Shift for the scan code

#define TERM_PARAMETERS_MAX_SIZE 20  // Maximum size of the parameters

// Display command to enter the terminal mode and ignore other keys
#define DISPLAY_COMMAND_TERM 0x3  // Enter terminal mode

#define TPRINTF(fmt, ...)                                 \
  do {                                                    \
    char buffer[256];                                     \
    snprintf(buffer, sizeof(buffer), fmt, ##__VA_ARGS__); \
    term_printString(buffer);                             \
  } while (0)

// Command lookup structure, now with argument support
typedef struct {
  const char *command;
  void (*handler)(const char *arg);
} Command;

void __not_in_flash_func(term_dma_irq_handler_lookup)(void);

void term_init(void);

/**
 * @brief Prints a string to the terminal with VT52 escape sequence processing.
 *
 * This function implements a simple state machine that looks for the ESC (0x1B)
 * character. When found, it starts buffering characters until a complete VT52
 * escape sequence is detected. For normal characters, it just calls
 * term_render_char.
 *
 * @param str The string to print.
 */
void term_printString(const char *str);

/**
 * @brief Clear the terminal display area
 *
 * Clear the terminal display area. Resets terminal state and removes all
 * previously rendered output, preparing the display for fresh content.
 */
void term_clearScreen(void);

/**
 * @brief Register terminal command handlers
 *
 * Register the array of terminal command handlers. Accepts a list of Command
 * structures linking command strings with their respective function pointers,
 * enabling interactive command processing at the terminal. Used to register new
 * commands.
 */
void term_setCommands(const Command *cmds, size_t count);
/**
 * @brief Clears the terminal's input buffer.
 *
 * This function removes any pending characters in the input buffer,
 * ensuring that subsequent input operations start with a clean state.
 */
void term_clearInputBuffer(void);

/**
 * @brief Retrieve the current terminal input buffer.
 *
 * This function returns a pointer to the buffer holding the input from the
 * terminal. The buffer is used for processing terminal input.
 *
 * @return char* A pointer to the terminal's input buffer.
 */
char *term_getInputBuffer(void);

// Generic commands to be used in the terminal
// Manage application setttings
void term_cmdSettings(const char *arg);
void term_cmdPrint(const char *arg);
void term_cmdSave(const char *arg);
void term_cmdErase(const char *arg);
void term_cmdGet(const char *arg);
void term_cmdPutInt(const char *arg);
void term_cmdPutBool(const char *arg);
void term_cmdPutString(const char *arg);

void __not_in_flash_func(term_loop)();

#endif  // TERML_H
