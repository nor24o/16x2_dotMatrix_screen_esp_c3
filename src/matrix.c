#include "matrix.h"
#include <string.h>
#include <ctype.h>
#include <strings.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "driver/gpio.h"
#include "soc/gpio_reg.h"
#include "esp_log.h"

static const char *TAG = "MATRIX";

#define REFRESH_INTERVAL_US 1200

#define DATA_HIGH()   REG_WRITE(GPIO_OUT_W1TS_REG, (1UL << PIN_DATA))
#define DATA_LOW()    REG_WRITE(GPIO_OUT_W1TC_REG, (1UL << PIN_DATA))
#define CLK_HIGH()    REG_WRITE(GPIO_OUT_W1TS_REG, (1UL << PIN_CLK))
#define CLK_LOW()     REG_WRITE(GPIO_OUT_W1TC_REG, (1UL << PIN_CLK))
#define STR_HIGH()    REG_WRITE(GPIO_OUT_W1TS_REG, (1UL << PIN_STR))
#define STR_LOW()     REG_WRITE(GPIO_OUT_W1TC_REG, (1UL << PIN_STR))
#define OE_HIGH()     REG_WRITE(GPIO_OUT_W1TS_REG, (1UL << PIN_OE))
#define OE_LOW()      REG_WRITE(GPIO_OUT_W1TC_REG, (1UL << PIN_OE))
#define ADDR_A_HIGH() REG_WRITE(GPIO_OUT_W1TS_REG, (1UL << PIN_ADDR_A))
#define ADDR_A_LOW()  REG_WRITE(GPIO_OUT_W1TC_REG, (1UL << PIN_ADDR_A))
#define ADDR_B_HIGH() REG_WRITE(GPIO_OUT_W1TS_REG, (1UL << PIN_ADDR_B))
#define ADDR_B_LOW()  REG_WRITE(GPIO_OUT_W1TC_REG, (1UL << PIN_ADDR_B))
#define ADDR_C_HIGH() REG_WRITE(GPIO_OUT_W1TS_REG, (1UL << PIN_ADDR_C))
#define ADDR_C_LOW()  REG_WRITE(GPIO_OUT_W1TC_REG, (1UL << PIN_ADDR_C))
#define BLANK_HIGH()  REG_WRITE(GPIO_OUT_W1TS_REG, (1UL << PIN_BLANK))
#define BLANK_LOW()   REG_WRITE(GPIO_OUT_W1TC_REG, (1UL << PIN_BLANK))

static uint8_t drawBuffer[MATRIX_ROWS][BUFFER_BYTES];
static volatile uint8_t displayBuffer[MATRIX_ROWS][BUFFER_BYTES];

static esp_timer_handle_t s_display_timer = NULL;
static esp_timer_handle_t s_oe_off_timer = NULL;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static volatile bool s_flip_h = false;
static volatile bool s_flip_v = false;
static volatile int s_brightness_us = 350;

// Standard 5x7 ASCII character bitmaps
static const uint8_t FONT_TEXT[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, // 0: Space
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, // 1: A
    {0x7F, 0x49, 0x49, 0x49, 0x36}, // 2: B
    {0x3E, 0x41, 0x41, 0x41, 0x22}, // 3: C
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, // 4: D
    {0x7F, 0x49, 0x49, 0x49, 0x41}, // 5: E
    {0x7F, 0x09, 0x09, 0x09, 0x01}, // 6: F
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, // 7: G
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, // 8: H
    {0x00, 0x41, 0x7F, 0x41, 0x00}, // 9: I
    {0x20, 0x40, 0x41, 0x3F, 0x01}, // 10: J
    {0x7F, 0x08, 0x14, 0x22, 0x41}, // 11: K
    {0x7F, 0x40, 0x40, 0x40, 0x40}, // 12: L
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, // 13: M
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, // 14: N
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, // 15: O
    {0x7F, 0x09, 0x09, 0x09, 0x06}, // 16: P
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, // 17: Q
    {0x7F, 0x09, 0x19, 0x29, 0x46}, // 18: R
    {0x46, 0x49, 0x49, 0x49, 0x31}, // 19: S
    {0x01, 0x01, 0x7F, 0x01, 0x01}, // 20: T
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, // 21: U
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, // 22: V
    {0x3F, 0x40, 0x38, 0x40, 0x3F}, // 23: W
    {0x63, 0x14, 0x08, 0x14, 0x63}, // 24: X
    {0x07, 0x08, 0x70, 0x08, 0x07}, // 25: Y
    {0x61, 0x51, 0x49, 0x45, 0x43}, // 26: Z
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, // 27: 0
    {0x00, 0x42, 0x7F, 0x40, 0x00}, // 28: 1
    {0x42, 0x61, 0x51, 0x49, 0x46}, // 29: 2
    {0x21, 0x41, 0x45, 0x4B, 0x31}, // 30: 3
    {0x18, 0x14, 0x12, 0x7F, 0x10}, // 31: 4
    {0x27, 0x45, 0x45, 0x45, 0x39}, // 32: 5
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 33: 6
    {0x01, 0x71, 0x09, 0x05, 0x03}, // 34: 7
    {0x36, 0x49, 0x49, 0x49, 0x36}, // 35: 8
    {0x06, 0x49, 0x49, 0x29, 0x1E}  // 36: 9
};

static const uint8_t FONT_BIG_DIGITS[][4] = {
    {0x7F, 0x41, 0x41, 0x7F}, // 0
    {0x00, 0x42, 0x7F, 0x40}, // 1
    {0x79, 0x49, 0x49, 0x4F}, // 2
    {0x41, 0x49, 0x49, 0x7F}, // 3
    {0x0F, 0x08, 0x08, 0x7F}, // 4
    {0x4F, 0x49, 0x49, 0x79}, // 5
    {0x7F, 0x49, 0x49, 0x79}, // 6
    {0x01, 0x01, 0x01, 0x7F}, // 7
    {0x7F, 0x49, 0x49, 0x7F}, // 8
    {0x4F, 0x49, 0x49, 0x7F}, // 9
    {0x00, 0x60, 0x60, 0x00}, // 10: .
    {0x08, 0x08, 0x08, 0x08}, // 11: -
    {0x00, 0x00, 0x00, 0x00}  // 12: space
};

// 20 Custom 5x7 Dot-Matrix Icons (5 columns of 8-bit vertical pixels)
static const uint8_t ICONS_LIB[ICON_COUNT][5] = {
    {0x42, 0x24, 0x3C, 0x24, 0x42}, // 0: ICON_SUN
    {0x0C, 0x1E, 0x3F, 0x33, 0x1E}, // 1: ICON_CLOUD
    {0x0C, 0x1E, 0x3B, 0x33, 0x20}, // 2: ICON_RAIN
    {0x06, 0x1C, 0x38, 0x0E, 0x03}, // 3: ICON_THUNDER
    {0x15, 0x0E, 0x1F, 0x0E, 0x15}, // 4: ICON_SNOW
    {0x24, 0x49, 0x24, 0x49, 0x24}, // 5: ICON_MIST
    {0x1E, 0x21, 0x21, 0x12, 0x0C}, // 6: ICON_MOON
    {0x7C, 0x46, 0x77, 0x46, 0x7C}, // 7: ICON_HOME
    {0x7E, 0x1C, 0x18, 0x18, 0x78}, // 8: ICON_BED
    {0x3E, 0x1C, 0x1C, 0x1C, 0x3E}, // 9: ICON_LIVING
    {0x0F, 0x29, 0x39, 0x29, 0x0F}, // 10: ICON_OFFICE
    {0x1C, 0x3E, 0x3F, 0x3E, 0x1C}, // 11: ICON_WATER
    {0x18, 0x3E, 0x2F, 0x3C, 0x18}, // 12: ICON_FIRE
    {0x30, 0x7F, 0x7D, 0x7F, 0x30}, // 13: ICON_THERMO
    {0x0E, 0x1F, 0x3E, 0x1F, 0x0E}, // 14: ICON_HEART
    {0x10, 0x1A, 0x7F, 0x1A, 0x10}, // 15: ICON_TREE
    {0x43, 0x22, 0x1C, 0x44, 0xC2}, // 16: ICON_FAN
    {0x7E, 0x42, 0x43, 0x42, 0x7E}, // 17: ICON_BATTERY
    {0x30, 0x7C, 0x7F, 0x7C, 0x30}, // 18: ICON_BELL
    {0x18, 0x3C, 0x7E, 0x3C, 0x18}  // 19: ICON_CAR
};

static const char *ICON_NAMES[ICON_COUNT] = {
    "sun", "cloud", "rain", "thunder", "snow", "mist", "moon", "home",
    "bed", "living", "office", "water", "fire", "thermo", "heart", "tree",
    "fan", "battery", "bell", "car"
};

const uint8_t *matrix_get_icon(uint8_t icon_id) {
    if (icon_id >= ICON_COUNT) icon_id = 0;
    return ICONS_LIB[icon_id];
}

const char *matrix_get_icon_name(uint8_t icon_id) {
    if (icon_id >= ICON_COUNT) icon_id = 0;
    return ICON_NAMES[icon_id];
}

uint8_t matrix_parse_icon_name(const char *name) {
    if (!name || !name[0]) return ICON_SUN;
    if (name[0] >= '0' && name[0] <= '9') {
        int v = atoi(name);
        if (v >= 0 && v < ICON_COUNT) return (uint8_t)v;
    }
    if (strcasecmp(name, "sun") == 0 || strcasecmp(name, "clear") == 0 || strcasecmp(name, "day") == 0) return ICON_SUN;
    if (strcasecmp(name, "cloud") == 0 || strcasecmp(name, "cld") == 0) return ICON_CLOUD;
    if (strcasecmp(name, "rain") == 0) return ICON_RAIN;
    if (strcasecmp(name, "thunder") == 0 || strcasecmp(name, "storm") == 0 || strcasecmp(name, "bolt") == 0) return ICON_THUNDER;
    if (strcasecmp(name, "snow") == 0 || strcasecmp(name, "frost") == 0 || strcasecmp(name, "ice") == 0) return ICON_SNOW;
    if (strcasecmp(name, "mist") == 0 || strcasecmp(name, "fog") == 0) return ICON_MIST;
    if (strcasecmp(name, "moon") == 0 || strcasecmp(name, "night") == 0) return ICON_MOON;
    if (strcasecmp(name, "home") == 0 || strcasecmp(name, "house") == 0 || strcasecmp(name, "indoor") == 0 || strcasecmp(name, "in") == 0) return ICON_HOME;
    if (strcasecmp(name, "bed") == 0 || strcasecmp(name, "bedroom") == 0) return ICON_BED;
    if (strcasecmp(name, "living") == 0 || strcasecmp(name, "sofa") == 0 || strcasecmp(name, "couch") == 0) return ICON_LIVING;
    if (strcasecmp(name, "office") == 0 || strcasecmp(name, "pc") == 0 || strcasecmp(name, "desk") == 0 || strcasecmp(name, "cpu") == 0) return ICON_OFFICE;
    if (strcasecmp(name, "water") == 0 || strcasecmp(name, "drop") == 0 || strcasecmp(name, "humidity") == 0 || strcasecmp(name, "boiler") == 0) return ICON_WATER;
    if (strcasecmp(name, "fire") == 0 || strcasecmp(name, "flame") == 0 || strcasecmp(name, "heat") == 0 || strcasecmp(name, "heater") == 0) return ICON_FIRE;
    if (strcasecmp(name, "thermo") == 0 || strcasecmp(name, "temp") == 0) return ICON_THERMO;
    if (strcasecmp(name, "heart") == 0) return ICON_HEART;
    if (strcasecmp(name, "tree") == 0 || strcasecmp(name, "outdoor") == 0 || strcasecmp(name, "out") == 0 || strcasecmp(name, "garden") == 0) return ICON_TREE;
    if (strcasecmp(name, "fan") == 0 || strcasecmp(name, "wind") == 0) return ICON_FAN;
    if (strcasecmp(name, "battery") == 0 || strcasecmp(name, "bat") == 0) return ICON_BATTERY;
    if (strcasecmp(name, "bell") == 0 || strcasecmp(name, "alert") == 0) return ICON_BELL;
    if (strcasecmp(name, "car") == 0 || strcasecmp(name, "garage") == 0) return ICON_CAR;
    return ICON_SUN;
}

void matrix_clear(void) {
    memset(drawBuffer, 0, sizeof(drawBuffer));
}

void matrix_clear_screen(uint8_t screen) {
    if (screen >= NUM_SCREENS) return;
    int start_vx = screen * MODS_PER_SCREEN * VIRTUAL_MOD_WIDTH;
    int end_vx = start_vx + (MODS_PER_SCREEN * VIRTUAL_MOD_WIDTH);
    for (int x = start_vx; x < end_vx; x++) {
        for (int y = 0; y < MATRIX_ROWS; y++) {
            matrix_set_pixel(x, y, 0);
        }
    }
}

void matrix_show(void) {
    portENTER_CRITICAL(&s_mux);
    memcpy((void *)displayBuffer, (void *)drawBuffer, sizeof(displayBuffer));
    portEXIT_CRITICAL(&s_mux);
}

int matrix_get_virtual_mod_start(uint8_t screen, uint8_t mod) {
    return ((screen * MODS_PER_SCREEN) + mod) * VIRTUAL_MOD_WIDTH;
}

static inline int get_physical_x(int vx) {
    int modIdx = vx / VIRTUAL_MOD_WIDTH;
    int offset = vx % VIRTUAL_MOD_WIDTH;
    if (offset >= MODULE_WIDTH) return -1;
    return (modIdx * MODULE_WIDTH) + offset;
}

void matrix_set_pixel(int virtualX, int y, uint8_t state) {
    if (virtualX < 0 || virtualX >= VIRTUAL_COLS || y < 0 || y >= MATRIX_ROWS) return;
    int physX = get_physical_x(virtualX);
    if (physX == -1) return;

    if (s_flip_h) physX = (PHYS_COLS - 1) - physX;
    if (s_flip_v) y = (MATRIX_ROWS - 1) - y;

    int byteIdx = physX / 8;
    int bitIdx = physX % 8;
    if (byteIdx < BUFFER_BYTES) {
        if (state) {
            drawBuffer[y][byteIdx] |= (1 << bitIdx);
        } else {
            drawBuffer[y][byteIdx] &= ~(1 << bitIdx);
        }
    }
}

static uint8_t get_char_index(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A' + 1;
    if (c >= '0' && c <= '9') return c - '0' + 27;
    return 0;
}

static void draw_glyph_at(int x, int min_vx, int max_vx, char c) {
    if (c == ' ') return;
    if (c >= 'a' && c <= 'z') c -= 32;

    if (c == ':') {
        if (x + 1 >= min_vx && x + 2 < max_vx) {
            matrix_set_pixel(x + 1, 2, 1);
            matrix_set_pixel(x + 1, 5, 1);
            matrix_set_pixel(x + 2, 2, 1);
            matrix_set_pixel(x + 2, 5, 1);
        }
        return;
    }
    if (c == '.') {
        if (x + 1 >= min_vx && x + 2 < max_vx) {
            matrix_set_pixel(x + 1, 7, 1);
            matrix_set_pixel(x + 2, 7, 1);
        }
        return;
    }
    if (c == '-') {
        for (int col = 1; col <= 3; col++) {
            if (x + col >= min_vx && x + col < max_vx) {
                matrix_set_pixel(x + col, 4, 1);
            }
        }
        return;
    }
    if (c == '%') {
        const int pts[6][2] = {{0,1},{3,1},{1,3},{2,4},{0,6},{3,6}};
        for (int i = 0; i < 6; i++) {
            int vx = x + pts[i][0];
            if (vx >= min_vx && vx < max_vx) {
                matrix_set_pixel(vx, pts[i][1], 1);
            }
        }
        return;
    }

    uint8_t idx = get_char_index(c);
    if (idx == 0) return;
    for (int col = 0; col < 5; col++) {
        int vx = x + col;
        if (vx >= min_vx && vx < max_vx) {
            uint8_t data = FONT_TEXT[idx][col];
            for (int r = 0; r < 8; r++) {
                if (data & (1 << r)) {
                    matrix_set_pixel(vx, r, 1);
                }
            }
        }
    }
}

void matrix_draw_char(uint8_t screen, uint8_t mod, char c) {
    if (screen >= NUM_SCREENS || mod >= MODS_PER_SCREEN) return;
    int startX = matrix_get_virtual_mod_start(screen, mod);
    draw_glyph_at(startX, 0, VIRTUAL_COLS, c);
}

void matrix_draw_string(uint8_t screen, uint8_t start_mod, const char *str) {
    if (screen >= NUM_SCREENS) return;
    int len = strlen(str);
    for (int i = 0; i < len && (start_mod + i) < MODS_PER_SCREEN; i++) {
        matrix_draw_char(screen, start_mod + i, str[i]);
    }
}

void matrix_draw_text_at(int x, const char *msg) {
    int len = strlen(msg);
    for (int i = 0; i < len; i++) {
        draw_glyph_at(x, 0, VIRTUAL_COLS, msg[i]);
        x += VIRTUAL_MOD_WIDTH;
    }
}

void matrix_draw_text_on_screen(uint8_t screen, int x, const char *msg) {
    if (screen >= NUM_SCREENS) return;
    int min_vx = screen * MODS_PER_SCREEN * VIRTUAL_MOD_WIDTH;
    int max_vx = (screen + 1) * MODS_PER_SCREEN * VIRTUAL_MOD_WIDTH;
    int len = strlen(msg);
    for (int i = 0; i < len; i++) {
        draw_glyph_at(x, min_vx, max_vx, msg[i]);
        x += VIRTUAL_MOD_WIDTH;
    }
}

static void draw_big_digit(uint8_t screen, uint8_t mod, char c) {
    int startX = matrix_get_virtual_mod_start(screen, mod);
    uint8_t idx = 12;
    if (c >= '0' && c <= '9') idx = c - '0';
    else if (c == '.') idx = 10;
    else if (c == '-') idx = 11;
    for (uint8_t col = 0; col < 4; col++) {
        uint8_t data = FONT_BIG_DIGITS[idx][col];
        for (uint8_t r = 0; r < 8; r++) {
            if (data & (1 << r)) {
                matrix_set_pixel(startX + col, r, 1);
            }
        }
    }
}

void matrix_draw_icon_raw(uint8_t screen, uint8_t mod, const uint8_t *icon_bytes) {
    if (screen >= NUM_SCREENS || mod >= MODS_PER_SCREEN || !icon_bytes) return;
    int startX = matrix_get_virtual_mod_start(screen, mod);
    for (uint8_t col = 0; col < 5; col++) {
        uint8_t data = icon_bytes[col];
        for (uint8_t r = 0; r < 8; r++) {
            if (data & (1 << r)) {
                matrix_set_pixel(startX + col, r, 1);
            }
        }
    }
}

void matrix_draw_icon(uint8_t screen, uint8_t mod, uint8_t icon_id) {
    matrix_draw_icon_raw(screen, mod, matrix_get_icon(icon_id));
}

static void draw_single_temp(uint8_t screen, uint8_t startMod, float temp) {
    float abs_t = (temp < 0) ? -temp : temp;
    int val = (int)(abs_t * 10.0f + 0.5f);
    int d3 = val % 10;
    int d2 = (val / 10) % 10;
    int d1 = (val / 100) % 10;

    if (temp <= -10.0f) {
        draw_big_digit(screen, startMod, '-');
        draw_big_digit(screen, startMod + 1, '0' + d1);
        draw_big_digit(screen, startMod + 2, '0' + d2);
        draw_big_digit(screen, startMod + 3, ' ');
    } else if (temp < 0.0f) {
        draw_big_digit(screen, startMod, '-');
        draw_big_digit(screen, startMod + 1, '0' + d2);
        draw_big_digit(screen, startMod + 2, '.');
        draw_big_digit(screen, startMod + 3, '0' + d3);
    } else {
        draw_big_digit(screen, startMod, (temp >= 10.0f) ? ('0' + d1) : ' ');
        draw_big_digit(screen, startMod + 1, '0' + d2);
        draw_big_digit(screen, startMod + 2, '.');
        draw_big_digit(screen, startMod + 3, '0' + d3);
    }
}

void matrix_draw_4temps_custom(float t1, float t2, float t3, float t4,
                               uint8_t i1, uint8_t i2, uint8_t i3, uint8_t i4) {
    draw_single_temp(0, 0, t1);
    matrix_draw_icon(0, 5, i1);
    draw_single_temp(0, 9, t2);
    matrix_draw_icon(0, 14, i2);
    draw_single_temp(1, 0, t3);
    matrix_draw_icon(1, 5, i3);
    draw_single_temp(1, 9, t4);
    matrix_draw_icon(1, 14, i4);
}

void matrix_draw_4temps(float t1, float t2, float t3, float t4) {
    matrix_draw_4temps_custom(t1, t2, t3, t4, ICON_SUN, ICON_CLOUD, ICON_RAIN, ICON_THUNDER);
}

void matrix_draw_night_temps(float t_left, uint8_t icon_left, float t_right, uint8_t icon_right) {
    matrix_clear_screen(1);
    if (t_left > -90.0f && t_left < 99.0f) {
        draw_single_temp(1, 0, t_left);
        matrix_draw_icon(1, 5, icon_left);
    }
    if (t_right > -90.0f && t_right < 99.0f) {
        draw_single_temp(1, 9, t_right);
        matrix_draw_icon(1, 14, icon_right);
    }
}

void matrix_screen_sweep(void) {
    for (int x = 0; x < VIRTUAL_COLS; x++) {
        matrix_clear();
        for (int y = 0; y < MATRIX_ROWS; y++) {
            matrix_set_pixel(x, y, 1);
        }
        matrix_show();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    matrix_clear();
    matrix_show();
}

void matrix_set_flip(bool flip_h, bool flip_v) {
    s_flip_h = flip_h;
    s_flip_v = flip_v;
}

void matrix_get_flip(bool *flip_h, bool *flip_v) {
    if (flip_h) *flip_h = s_flip_h;
    if (flip_v) *flip_v = s_flip_v;
}

void matrix_set_brightness(int us) {
    if (us < 10) us = 10;
    if (us > 800) us = 800;
    s_brightness_us = us;
}

int matrix_get_brightness(void) {
    return s_brightness_us;
}

// -------------------------------------------------------------
// Zero-Flicker Non-Blocking Dual Hardware Timer Multiplexer
// - row_timer_callback shifts 20 bytes in ~15us and starts OE pulse
// - oe_off_timer_callback turns OE off after exact pulse_us (0% busy-wait!)
// -------------------------------------------------------------
static void IRAM_ATTR oe_off_timer_callback(void* arg) {
    OE_HIGH();
}

static void IRAM_ATTR row_timer_callback(void* arg) {
    static uint8_t currentRow = 0;

    // 0. Ensure OE is blanked before shifting
    esp_timer_stop(s_oe_off_timer);
    OE_HIGH();

    portENTER_CRITICAL_SAFE(&s_mux);

    // 1. Shift all 20 bytes (160 physical bits, LSB first)
    for (int b = 0; b < BUFFER_BYTES; b++) {
        uint8_t data = displayBuffer[currentRow][b];
        for (int i = 0; i < 8; i++) {
            CLK_LOW();
            if (data & 1) {
                DATA_HIGH();
            } else {
                DATA_LOW();
            }
            data >>= 1;
            __asm__ volatile("nop; nop; nop; nop;");
            CLK_HIGH();
            __asm__ volatile("nop; nop; nop; nop;");
        }
    }
    CLK_LOW();

    // 2. Select Row Address (3-to-8 decoder)
    uint8_t addr = 7 - currentRow;
    if (addr & 0x01) ADDR_A_HIGH(); else ADDR_A_LOW();
    if (addr & 0x02) ADDR_B_HIGH(); else ADDR_B_LOW();
    if (addr & 0x04) ADDR_C_HIGH(); else ADDR_C_LOW();

    // 3. Strobe Latch
    STR_HIGH();
    __asm__ volatile("nop; nop; nop; nop; nop; nop; nop; nop;");
    STR_LOW();

    portEXIT_CRITICAL_SAFE(&s_mux);

    // 4. Illuminate row via hardware one-shot timer (zero CPU blocking!)
    int pulse = s_brightness_us;
    if (pulse > (REFRESH_INTERVAL_US - 150)) {
        pulse = REFRESH_INTERVAL_US - 150;
    }

    if (pulse >= 15) {
        OE_LOW();
        esp_timer_start_once(s_oe_off_timer, pulse);
    } else if (pulse > 0) {
        OE_LOW();
        esp_rom_delay_us(pulse);
        OE_HIGH();
    }

    currentRow = (currentRow + 1) & 0x07;
}

void matrix_init(void) {
    ESP_LOGI(TAG, "Initializing Matrix Display (32 modules, 20 bytes/row)...");

    // 1. Reset all pins from any peripheral functions (e.g. UART0 on GPIO 21)
    gpio_reset_pin(PIN_DATA);
    gpio_reset_pin(PIN_CLK);
    gpio_reset_pin(PIN_STR);
    gpio_reset_pin(PIN_OE);
    gpio_reset_pin(PIN_ADDR_A);
    gpio_reset_pin(PIN_ADDR_B);
    gpio_reset_pin(PIN_ADDR_C);
    gpio_reset_pin(PIN_BLANK);

    // 2. Configure as clean push-pull outputs
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_DATA) | (1ULL << PIN_CLK) | (1ULL << PIN_STR) |
                        (1ULL << PIN_OE) | (1ULL << PIN_ADDR_A) | (1ULL << PIN_ADDR_B) |
                        (1ULL << PIN_ADDR_C) | (1ULL << PIN_BLANK),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    // 3. Set safe initial pin states
    OE_HIGH();
    BLANK_HIGH();
    STR_LOW();
    CLK_LOW();
    DATA_LOW();

    matrix_clear();
    matrix_show();

    // 4. Create one-shot hardware timer for OE pulse termination
    const esp_timer_create_args_t oe_timer_args = {
        .callback = &oe_off_timer_callback,
        .name = "oe_off_timer"
    };
    ESP_ERROR_CHECK(esp_timer_create(&oe_timer_args, &s_oe_off_timer));

    // 5. Start periodic hardware timer for display row multiplexing (1200us = 104Hz frame rate)
    const esp_timer_create_args_t row_timer_args = {
        .callback = &row_timer_callback,
        .name = "disp_row_timer"
    };
    ESP_ERROR_CHECK(esp_timer_create(&row_timer_args, &s_display_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_display_timer, REFRESH_INTERVAL_US));

    ESP_LOGI(TAG, "Matrix Non-Blocking Multiplexer started at %d us interval (104 Hz).", REFRESH_INTERVAL_US);
}
