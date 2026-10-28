#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NUM_SCREENS         2
#define MODS_PER_SCREEN     16
#define TOTAL_MODULES       32
#define MODULE_WIDTH        5
#define MATRIX_ROWS         8
#define PHYS_COLS           160   // 32 * 5
#define BUFFER_BYTES        20    // 160 / 8

#define GAP_WIDTH           1
#define VIRTUAL_MOD_WIDTH   (MODULE_WIDTH + GAP_WIDTH)  // 6
#define VIRTUAL_COLS        (TOTAL_MODULES * VIRTUAL_MOD_WIDTH) // 192

// Pin definitions (matched to hardware wiring from backup firmware)
#define PIN_DATA     4
#define PIN_CLK      6
#define PIN_STR      3
#define PIN_OE       7
#define PIN_ADDR_A   2
#define PIN_ADDR_B   21
#define PIN_ADDR_C   0
#define PIN_BLANK    5

// Initialization & Control
void matrix_init(void);
void matrix_clear(void);
void matrix_show(void);
void matrix_set_pixel(int virtualX, int y, uint8_t state);
void matrix_draw_char(uint8_t screen, uint8_t mod, char c);
void matrix_draw_string(uint8_t screen, uint8_t start_mod, const char *str);
void matrix_screen_sweep(void);

// Settings
void matrix_set_flip(bool flip_h, bool flip_v);
void matrix_get_flip(bool *flip_h, bool *flip_v);
void matrix_set_brightness(int us);
int  matrix_get_brightness(void);

#ifdef __cplusplus
}
#endif
