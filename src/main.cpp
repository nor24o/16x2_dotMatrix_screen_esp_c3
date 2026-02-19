#include <Arduino.h>
#include <soc/gpio_struct.h>

// ==========================================
// 1. CONFIGURATION
// ==========================================
const bool ENABLE_GAPS = true;
int BRIGHTNESS = 400; 
int REFRESH_INTERVAL_US = 900; 

// ==========================================
// 2. PIN DEFINITIONS (FIXED)
// ==========================================
#define DATA_PIN    4  
#define CLK_PIN     21 
#define STR_PIN     3  
#define OE_PIN      7  
#define ADDR_A      2  
#define ADDR_B      6  
#define ADDR_C      0  
#define G1_BLANK    5  

// --- FAST GPIO MACROS ---
#define DATA_HIGH()   GPIO.out_w1ts.val = (1UL << DATA_PIN)
#define DATA_LOW()    GPIO.out_w1tc.val = (1UL << DATA_PIN)
#define CLK_HIGH()    GPIO.out_w1ts.val = (1UL << CLK_PIN)
#define CLK_LOW()     GPIO.out_w1tc.val = (1UL << CLK_PIN)
#define STR_HIGH()    GPIO.out_w1ts.val = (1UL << STR_PIN)
#define STR_LOW()     GPIO.out_w1tc.val = (1UL << STR_PIN)
#define OE_HIGH()     GPIO.out_w1ts.val = (1UL << OE_PIN)
#define OE_LOW()      GPIO.out_w1tc.val = (1UL << OE_PIN)
#define ADDR_A_HIGH() GPIO.out_w1ts.val = (1UL << ADDR_A)
#define ADDR_A_LOW()  GPIO.out_w1tc.val = (1UL << ADDR_A)
#define ADDR_B_HIGH() GPIO.out_w1ts.val = (1UL << ADDR_B)
#define ADDR_B_LOW()  GPIO.out_w1tc.val = (1UL << ADDR_B)
#define ADDR_C_HIGH() GPIO.out_w1ts.val = (1UL << ADDR_C)
#define ADDR_C_LOW()  GPIO.out_w1tc.val = (1UL << ADDR_C)
#define BLANK_HIGH()  GPIO.out_w1ts.val = (1UL << G1_BLANK)
#define BLANK_LOW()   GPIO.out_w1tc.val = (1UL << G1_BLANK)

// ==========================================
// 3. MATRIX SETTINGS
// ==========================================
const uint8_t NUM_SCREENS = 2;        
const uint8_t MODS_PER_SCREEN = 16;   
const uint8_t TOTAL_MODULES = 32;     
const uint8_t MODULE_WIDTH = 5;       
const uint8_t ROWS = 8;
const uint16_t PHYS_COLS = 160;
const uint8_t BUFFER_BYTES = 20;      

const uint8_t GAP_WIDTH = 1;
const uint16_t VIRTUAL_MOD_WIDTH = (ENABLE_GAPS ? (MODULE_WIDTH + GAP_WIDTH) : MODULE_WIDTH);
const uint16_t VIRTUAL_COLS = TOTAL_MODULES * VIRTUAL_MOD_WIDTH;

volatile uint8_t displayBuffer[ROWS][BUFFER_BYTES];
hw_timer_t *timer = NULL;

// ==========================================
// 4. FONTS
// ==========================================
const uint8_t FONT_BIG_DIGITS[][4] = {
    {0x7F, 0x41, 0x41, 0x7F}, {0x00, 0x42, 0x7F, 0x40}, {0x79, 0x49, 0x49, 0x4F},
    {0x41, 0x49, 0x49, 0x7F}, {0x0F, 0x08, 0x08, 0x7F}, {0x4F, 0x49, 0x49, 0x79},
    {0x7F, 0x49, 0x49, 0x79}, {0x01, 0x01, 0x01, 0x7F}, {0x7F, 0x49, 0x49, 0x7F},
    {0x4F, 0x49, 0x49, 0x7F}, {0x00, 0x60, 0x60, 0x00}, {0x08, 0x08, 0x08, 0x08},
    {0x00, 0x00, 0x00, 0x00}
};
const uint8_t ICON_SUN_MOD[]     = {0x42, 0x24, 0x3C, 0x24, 0x42};
const uint8_t ICON_CLOUD_MOD[]   = {0x0C, 0x1E, 0x3F, 0x33, 0x1E};
const uint8_t ICON_RAIN_MOD[]    = {0x0C, 0x1E, 0x3B, 0x33, 0x20};
const uint8_t ICON_THUNDER_MOD[] = {0x06, 0x1C, 0x38, 0x0E, 0x03};
const uint8_t ICON_MIST_MOD[]    = {0x24, 0x49, 0x24, 0x49, 0x24};
const uint8_t FONT_TEXT[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, {0x7E, 0x11, 0x11, 0x11, 0x7E},
    {0x7F, 0x49, 0x49, 0x49, 0x36}, {0x3E, 0x41, 0x41, 0x41, 0x22},
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, {0x7F, 0x49, 0x49, 0x49, 0x41},
    {0x7F, 0x09, 0x09, 0x09, 0x01}, {0x3E, 0x41, 0x49, 0x49, 0x7A},
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, {0x00, 0x41, 0x7F, 0x41, 0x00},
    {0x20, 0x40, 0x41, 0x3F, 0x01}, {0x7F, 0x08, 0x14, 0x22, 0x41},
    {0x7F, 0x40, 0x40, 0x40, 0x40}, {0x7F, 0x02, 0x0C, 0x02, 0x7F},
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, {0x3E, 0x41, 0x41, 0x41, 0x3E},
    {0x7F, 0x09, 0x09, 0x09, 0x06}, {0x3E, 0x41, 0x51, 0x21, 0x5E},
    {0x7F, 0x09, 0x19, 0x29, 0x46}, {0x46, 0x49, 0x49, 0x49, 0x31},
    {0x01, 0x01, 0x7F, 0x01, 0x01}, {0x3F, 0x40, 0x40, 0x40, 0x3F},
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, {0x3F, 0x40, 0x38, 0x40, 0x3F},
    {0x63, 0x14, 0x08, 0x14, 0x63}, {0x07, 0x08, 0x70, 0x08, 0x07},
    {0x61, 0x51, 0x49, 0x45, 0x43}, {0x3E, 0x41, 0x41, 0x41, 0x3E},
    {0x00, 0x42, 0x7F, 0x40, 0x00}, {0x42, 0x61, 0x51, 0x49, 0x46},
    {0x21, 0x41, 0x45, 0x4B, 0x31}, {0x18, 0x14, 0x12, 0x7F, 0x10},
    {0x27, 0x45, 0x45, 0x45, 0x39}, {0x3C, 0x4A, 0x49, 0x49, 0x30},
    {0x01, 0x71, 0x09, 0x05, 0x03}, {0x36, 0x49, 0x49, 0x49, 0x36},
    {0x06, 0x49, 0x49, 0x29, 0x1E}
};

// ==========================================
// 5. GRAPHICS ENGINE
// ==========================================
void clearBuffer() {
    memset((void*)displayBuffer, 0, sizeof(displayBuffer));
}
int getPhysicalX(int vx) {
    if (!ENABLE_GAPS) return vx;
    int modIdx = vx / VIRTUAL_MOD_WIDTH;
    int offset = vx % VIRTUAL_MOD_WIDTH;
    if (offset >= MODULE_WIDTH) return -1;
    return (modIdx * MODULE_WIDTH) + offset;
}
void setPixel(int virtualX, int y, uint8_t state) {
    if (virtualX < 0 || virtualX >= VIRTUAL_COLS || y < 0 || y >= ROWS) return;
    int physX = getPhysicalX(virtualX);
    if (physX == -1) return;
    int byteIdx = physX / 8;
    int bitIdx = physX % 8;
    if (byteIdx < BUFFER_BYTES) {
        if (state) displayBuffer[y][byteIdx] |= (1 << bitIdx);
        else       displayBuffer[y][byteIdx] &= ~(1 << bitIdx);
    }
}
int getVirtualModuleStart(uint8_t screen, uint8_t mod) {
    int globalMod = (screen * MODS_PER_SCREEN) + mod;
    return globalMod * VIRTUAL_MOD_WIDTH;
}
void drawChar(uint8_t screen, uint8_t mod, char c) {
    int startX = getVirtualModuleStart(screen, mod);
    uint8_t idx = 12; // Space
    if (c >= '0' && c <= '9') idx = c - '0';
    if (c == '.') idx = 10;
    if (c == '-') idx = 11;
    for (uint8_t col = 0; col < 4; col++) {
        uint8_t data = FONT_BIG_DIGITS[idx][col];
        for (uint8_t r = 0; r < 8; r++) {
            if (data & (1 << r)) setPixel(startX + col, r, 1);
        }
    }
}
void drawIcon(uint8_t screen, uint8_t mod, const uint8_t* icon) {
    int startX = getVirtualModuleStart(screen, mod);
    for (uint8_t col = 0; col < 5; col++) {
        uint8_t data = icon[col];
        for (uint8_t r = 0; r < 8; r++) {
            if (data & (1 << r)) setPixel(startX + col, r, 1);
        }
    }
}
void drawTemp(uint8_t screen, uint8_t startMod, float temp) {
    int val = (int)(abs(temp) * 10);
    int d3 = val % 10;           
    int d2 = (val / 10) % 10;    
    int d1 = (val / 100) % 10;   
    if (temp < 0) {
        drawChar(screen, startMod, '-');
        drawChar(screen, startMod+1, (temp <= -10) ? '0'+d1 : '0'+d2);
    } else {
        if (temp >= 10) drawChar(screen, startMod, '0'+d1);
        else            drawChar(screen, startMod, ' ');
        drawChar(screen, startMod+1, '0'+d2);
    }
    drawChar(screen, startMod+2, '.');
    drawChar(screen, startMod+3, '0'+d3);
}
uint8_t getCharIdxText(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A' + 1;
    if (c >= '0' && c <= '9') return c - '0' + 27;
    return 0;
}
void drawTextAt(int x, const char* msg) {
    int len = strlen(msg);
    for(int i=0; i<len; i++) {
        uint8_t idx = getCharIdxText(msg[i]);
        for(int col=0; col<5; col++) {
             uint8_t data = FONT_TEXT[idx][col];
             for(int r=0; r<8; r++) if(data & (1<<r)) setPixel(x+col, r, 1);
        }
        x += 6;
    }
}

// ==========================================
// 6. ANIMATIONS
// ==========================================
void animSparkle(int speed) {
    for (int i = 0; i < 800; i++) {
        setPixel(random(0, VIRTUAL_COLS), random(0, ROWS), 0);
        delay(speed);
    }
    clearBuffer();
}
void animScanner(int loops) {
    for(int l=0; l<loops; l++) {
        for(int i=0; i<VIRTUAL_COLS-4; i+=2) {
            clearBuffer();
            for(int w=0; w<4; w++) for(int h=0; h<8; h++) setPixel(i+w, h, 1);
            delay(5);
        }
        for(int i=VIRTUAL_COLS-4; i>0; i-=2) {
            clearBuffer();
            for(int w=0; w<4; w++) for(int h=0; h<8; h++) setPixel(i+w, h, 1);
            delay(5);
        }
    }
}
void animScroll(const char* txt) {
    int len = strlen(txt);
    for(int x=VIRTUAL_COLS; x>-(len*6); x--) {
        clearBuffer();
        drawTextAt(x, txt);
        delay(20);
    }
}
void animTypewriter(const char* msg) {
    clearBuffer();
    int len = strlen(msg);
    int startX = (len * 6 < VIRTUAL_COLS) ? (VIRTUAL_COLS - (len * 6)) / 2 : 0;
    //startX =startX-70;
    char buf[2] = {0};
    for(int i=0; i<len; i++) {
        buf[0] = msg[i];
        drawTextAt(startX + (i*6), buf);
        delay(80);
    }
    delay(3000);
}

// ==========================================
// 7. SETUP & ISR
// ==========================================
void IRAM_ATTR onTimer() {
    static uint8_t currentRow = 0;

    // 1. Blank Output
    OE_HIGH(); 

    // 2. Select Row (Address ABC)
    // Vertically flipped logic: 7 - currentRow
    uint8_t addr = 7 - currentRow;
    if (addr & 0x01) ADDR_A_HIGH(); else ADDR_A_LOW();
    if (addr & 0x02) ADDR_B_HIGH(); else ADDR_B_LOW();
    if (addr & 0x04) ADDR_C_HIGH(); else ADDR_C_LOW();

    // Row Stabilization
    for(volatile int k=0; k<5; k++) __asm__("nop");

    // 3. Shift Out Data
    // FIXED: Original loop order (0 to End) so text is NOT backwards
    for (int8_t b = 0; b < BUFFER_BYTES; b++) {
        uint8_t data = displayBuffer[currentRow][b];
        for (int8_t i = 0; i < 8; i++) {
            CLK_LOW();
            
            // Set Data
            if (data & (1 << i)) DATA_HIGH();
            else                DATA_LOW();

            // Setup Time Delay
            for(volatile int k=0; k<4; k++) __asm__("nop"); 
            
            CLK_HIGH();
            
            // Hold Time Delay
            for(volatile int k=0; k<4; k++) __asm__("nop"); 
        }
    }

    // 4. Latch Data
    STR_HIGH();
    // Latch Duration Delay
    for(volatile int k=0; k<4; k++) __asm__("nop"); 
    STR_LOW();

    // 5. Unblank (Display ON) & PWM Brightness
    OE_LOW(); // ON
    
    if (BRIGHTNESS > 0) {
        long loops = BRIGHTNESS * 10; 
        while(loops--) __asm__("nop");
    }
    
    OE_HIGH(); // OFF

    // 6. Next Row
    currentRow = (currentRow + 1) & 0x07;
}

void setup() {
    // Pin Setup
    pinMode(DATA_PIN, OUTPUT); pinMode(CLK_PIN, OUTPUT);
    pinMode(STR_PIN, OUTPUT);  pinMode(OE_PIN, OUTPUT);
    pinMode(ADDR_A, OUTPUT);   pinMode(ADDR_B, OUTPUT);
    pinMode(ADDR_C, OUTPUT);   pinMode(G1_BLANK, OUTPUT);
    
    OE_HIGH(); 
    BLANK_HIGH(); // Keep G1 high

    // --- TIMER SETUP ---
    timer = timerBegin(1000000); 
    timerAttachInterrupt(timer, &onTimer);
    timerAlarm(timer, REFRESH_INTERVAL_US, true, 0);
}

// ==========================================
// 8. MAIN LOOP
// ==========================================
void loop() {
    // 1. Text Animation
    animTypewriter("ESP32 OK     HOW ARE  YOU?");
    animSparkle(2);

    // 2. Scanner
    animScanner(2);

    // 3. SEPARATE SCREENS MODE
    clearBuffer();
    
    // -- Screen 0 (Left) --
    drawTemp(0, 0, 24.5);             
    drawIcon(0, 5, ICON_SUN_MOD);     
    drawTemp(0, 9, 18.2);             
    drawIcon(0, 14, ICON_CLOUD_MOD);

    // -- Screen 1 (Right) --
    drawTemp(1, 0, 12.0);
    drawIcon(1, 5, ICON_RAIN_MOD);
    
    drawTemp(1, 9, -5.5);
    drawIcon(1, 14, ICON_THUNDER_MOD);
    

    delay(5000);

    // 4. Scrolling News
    animScroll("DUAL SCREEN CONFIGURATION... GAPS ENABLED... RUNNING ON RISC-V...");
}