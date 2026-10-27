#include "matrix.h"
#include <string.h>
#include <ctype.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "driver/gpio.h"
#include "soc/gpio_reg.h"
#include "esp_log.h"

static const char *TAG = "MATRIX";

#define REFRESH_INTERVAL_US 1500

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

static TaskHandle_t s_display_task_handle = NULL;
static esp_timer_handle_t s_display_timer = NULL;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static volatile bool s_flip_h = false;
static volatile bool s_flip_v = false;
static volatile int s_brightness_us = 400;

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

void matrix_clear(void) {
    memset(drawBuffer, 0, sizeof(drawBuffer));
}

void matrix_show(void) {
    portENTER_CRITICAL(&s_mux);
    memcpy((void *)displayBuffer, (void *)drawBuffer, sizeof(displayBuffer));
    portEXIT_CRITICAL(&s_mux);
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

void matrix_draw_char(uint8_t screen, uint8_t mod, char c) {
    if (screen >= NUM_SCREENS || mod >= MODS_PER_SCREEN) return;
    int totalModIdx = (screen * MODS_PER_SCREEN) + mod;
    int startX = totalModIdx * VIRTUAL_MOD_WIDTH;

    if (c >= 'a' && c <= 'z') c -= 32;

    if (c == ':') {
        matrix_set_pixel(startX + 1, 2, 1);
        matrix_set_pixel(startX + 1, 5, 1);
        matrix_set_pixel(startX + 2, 2, 1);
        matrix_set_pixel(startX + 2, 5, 1);
        return;
    }
    if (c == '.') {
        matrix_set_pixel(startX + 1, 7, 1);
        matrix_set_pixel(startX + 2, 7, 1);
        return;
    }
    if (c == '-') {
        matrix_set_pixel(startX + 1, 4, 1);
        matrix_set_pixel(startX + 2, 4, 1);
        matrix_set_pixel(startX + 3, 4, 1);
        return;
    }

    uint8_t idx = get_char_index(c);
    if (idx == 0 && c != ' ') return;
    for (int col = 0; col < 5; col++) {
        uint8_t data = FONT_TEXT[idx][col];
        for (int r = 0; r < 8; r++) {
            if (data & (1 << r)) {
                matrix_set_pixel(startX + col, r, 1);
            }
        }
    }
}

void matrix_draw_string(uint8_t screen, uint8_t start_mod, const char *str) {
    if (screen >= NUM_SCREENS) return;
    int len = strlen(str);
    for (int i = 0; i < len && (start_mod + i) < MODS_PER_SCREEN; i++) {
        matrix_draw_char(screen, start_mod + i, str[i]);
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
    if (us > (REFRESH_INTERVAL_US - 200)) us = REFRESH_INTERVAL_US - 200;
    s_brightness_us = us;
}

int matrix_get_brightness(void) {
    return s_brightness_us;
}

// -------------------------------------------------------------
// FreeRTOS Display Multiplexer Task (Flicker-Free, High Priority)
// -------------------------------------------------------------
static void display_refresh_task(void *pvParameters) {
    uint8_t currentRow = 0;

    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        portENTER_CRITICAL(&s_mux);
        OE_HIGH(); // 0. Blank LEDs before row switch

        // 1. Shift all 20 bytes (160 physical bits)
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
                asm volatile("nop");
                CLK_HIGH();
                asm volatile("nop");
            }
        }

        // 2. Select Row Address (3-to-8 decoder)
        uint8_t addr = 7 - currentRow;
        if (addr & 0x01) ADDR_A_HIGH(); else ADDR_A_LOW();
        if (addr & 0x02) ADDR_B_HIGH(); else ADDR_B_LOW();
        if (addr & 0x04) ADDR_C_HIGH(); else ADDR_C_LOW();

        // 3. Strobe Latch
        STR_HIGH();
        asm volatile("nop; nop; nop; nop;");
        STR_LOW();
        esp_rom_delay_us(2);

        // 4. Illuminate for brightness duration
        int pulse = s_brightness_us;
        if (pulse > (REFRESH_INTERVAL_US - 200)) {
            pulse = REFRESH_INTERVAL_US - 200;
        }

        if (pulse > 0) {
            OE_LOW();
            esp_rom_delay_us(pulse);
            OE_HIGH();
        }

        portEXIT_CRITICAL(&s_mux);

        currentRow = (currentRow + 1) & 0x07;
    }
}

static void IRAM_ATTR timer_callback(void* arg) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    vTaskNotifyGiveFromISR(s_display_task_handle, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

void matrix_init(void) {
    ESP_LOGI(TAG, "Initializing Matrix Display (32 modules, 20 bytes/row)...");

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

    OE_HIGH();
    BLANK_HIGH();

    matrix_clear();
    matrix_show();

    // Priority 6: Above background and networking tasks
    xTaskCreate(display_refresh_task, "DispMuxTask", 4096, NULL, 6, &s_display_task_handle);

    const esp_timer_create_args_t timer_args = {
        .callback = &timer_callback,
        .name = "disp_timer"
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_display_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_display_timer, REFRESH_INTERVAL_US));

    ESP_LOGI(TAG, "Matrix Multiplexing started at %d us interval.", REFRESH_INTERVAL_US);
}
