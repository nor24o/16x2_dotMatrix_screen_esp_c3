#include <Arduino.h>
#include <soc/gpio_struct.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <time.h>
#include <esp_sntp.h>    // Added for custom NTP sync intervals
#include <HTTPClient.h>  // Added for fetching weather
#include <ArduinoJson.h> // Added for reading weather data

// ==========================================
// 1. CONFIGURATION
// ==========================================
const bool ENABLE_GAPS = true;
int BRIGHTNESS = 1000; 
int REFRESH_INTERVAL_US = 1200; 

// --- NETWORK & TIME CONFIG ---
const char* WIFI_SSID = "Link2";
const char* WIFI_PASSWORD = "Horvat2017";

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 7200;       
const int   daylightOffset_sec = 3600;  

char ipString[30] = "WAITING FOR IP...";

// --- WEATHER CONFIG ---
char liveWeatherString[100] = "WAITING FOR WEATHER...   ";
unsigned long lastWeatherUpdate = 0;
const unsigned long weatherUpdateInterval = 30 * 60 * 1000; // Update every 30 minutes

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
// 5. GRAPHICS ENGINE & HELPERS
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

uint8_t getCharIdxText(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A' + 1;
    if (c >= '0' && c <= '9') return c - '0' + 27;
    if (c == '%') return 36; // Using standard icon mapping if % isn't defined, replace if needed
    if (c == '.') return 0; 
    return 0;
}

void drawChar(uint8_t screen, uint8_t mod, char c) {
    int startX = getVirtualModuleStart(screen, mod);
    uint8_t idx = 12; 
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

void drawModuleChar(uint8_t screen, uint8_t mod, char c) {
    if (c == ':') {
        int startX = getVirtualModuleStart(screen, mod);
        setPixel(startX + 1, 2, 1); setPixel(startX + 1, 5, 1);
        setPixel(startX + 2, 2, 1); setPixel(startX + 2, 5, 1);
        return;
    }
    
    uint8_t idx = getCharIdxText(c);
    if (idx == 0 && c != ' ') return; 
    
    int startX = getVirtualModuleStart(screen, mod);
    for(int col=0; col<5; col++) {
        uint8_t data = FONT_TEXT[idx][col];
        for(int r=0; r<8; r++) {
            if (data & (1<<r)) setPixel(startX + col, r, 1);
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

void drawTextAt(int x, const char* msg) {
    int len = strlen(msg);
    for(int i=0; i<len; i++) {
        uint8_t idx = getCharIdxText(msg[i]);
        if (msg[i] == '.') {
            setPixel(x+1, 7, 1); 
            x += 3;
            continue;
        }
        if (msg[i] == '%') {
            // Draw a tiny % manually if font lacks it
            setPixel(x, 1, 1); setPixel(x+3, 1, 1);
            setPixel(x+1, 3, 1); setPixel(x+2, 4, 1);
            setPixel(x, 6, 1); setPixel(x+3, 6, 1);
            x += 5;
            continue;
        }
        for(int col=0; col<5; col++) {
             uint8_t data = FONT_TEXT[idx][col];
             for(int r=0; r<8; r++) if(data & (1<<r)) setPixel(x+col, r, 1);
        }
        x += 6;
    }
}

String normalizeCityName(String name) {
    // 1. Replace Romanian diacritics with basic letters
    name.replace("ă", "A"); name.replace("Ă", "A");
    name.replace("â", "A"); name.replace("Â", "A");
    name.replace("î", "I"); name.replace("Î", "I");
    // Handle both the correct comma-below and the older cedilla versions
    name.replace("ș", "S"); name.replace("Ș", "S");
    name.replace("ş", "S"); name.replace("Ş", "S"); 
    name.replace("ț", "T"); name.replace("Ț", "T");
    name.replace("ţ", "T"); name.replace("Ţ", "T");
    
    // 2. Force standard characters to uppercase
    name.toUpperCase();
    
    // 3. Strip out ANYTHING that isn't A-Z, 0-9, space, or a hyphen
    String cleanName = "";
    for (int i = 0; i < name.length(); i++) {
        char c = name[i];
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '-') {
            cleanName += c;
        }
    }
    return cleanName;
}

const char* getWeatherCondition(int code) {
    if (code == 0) return "CLEAR";
    if (code == 1 || code == 2) return "PARTLY CLOUDY";
    if (code == 3) return "OVERCAST";
    if (code >= 45 && code <= 48) return "FOGGY";
    if (code >= 51 && code <= 67) return "RAIN";
    if (code >= 71 && code <= 77) return "SNOW";
    if (code >= 80 && code <= 82) return "SHOWERS";
    if (code >= 95 && code <= 99) return "THUNDERSTORM";
    return "UNKNOWN";
}

// ==========================================
// 6. NETWORK FETCH FUNCTIONS
// ==========================================
// You can easily change this target city to anything you want!
// Change these whenever you want to track a different location
String targetCityName = "Sauca"; 
String targetCountryCode = "RO";   // Lock it to Romania
String targetRegion = "Satu Mare"; // Lock it to the specific county

void fetchWeather() {
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        
        // --- STEP 1: Look up the city name (Ask for 10 results instead of 1) ---
        String geoUrl = "http://geocoding-api.open-meteo.com/v1/search?name=" + targetCityName + "&count=10";
        http.begin(geoUrl);
        int geoResponseCode = http.GET();
        
        if (geoResponseCode == 200) {
            String geoPayload = http.getString();
            JsonDocument geoDoc; 
            DeserializationError geoError = deserializeJson(geoDoc, geoPayload);
            
            if (!geoError && geoDoc["results"].size() > 0) {
                float lat = 0.0;
                float lon = 0.0;
                String officialName = "";
                bool matchFound = false;

                // Loop through the results to find our exact Romanian village
                JsonArray results = geoDoc["results"];
                for (JsonObject result : results) {
                    String cCode = result["country_code"].as<String>();
                    String admin1 = result["admin1"].as<String>(); 
                    
                    // Does the country match? (e.g., "RO")
                    if (cCode == targetCountryCode) {
                        // Does the region match? (e.g., "Satu Mare")
                        // If targetRegion is left empty "", it will just take the first match in the country
                        if (targetRegion == "" || admin1.indexOf(targetRegion) >= 0) {
                            lat = result["latitude"];
                            lon = result["longitude"];
                            String rawName = result["name"].as<String>();
                            officialName = normalizeCityName(rawName);
                            matchFound = true;
                            break; // Stop looking, we found the right Sauca!
                        }
                    }
                }

                http.end(); // Close the first connection

                if (matchFound) {
                    // --- STEP 2: Fetch the weather using the CORRECT coordinates ---
                    String weatherUrl = "http://api.open-meteo.com/v1/forecast?latitude=" + String(lat, 4) + "&longitude=" + String(lon, 4) + "&current=temperature_2m,relative_humidity_2m,weather_code";
                    http.begin(weatherUrl);
                    int weatherResponseCode = http.GET();
                    
                    if (weatherResponseCode == 200) {
                        String weatherPayload = http.getString();
                        JsonDocument weatherDoc;
                        DeserializationError weatherError = deserializeJson(weatherDoc, weatherPayload);
                        
                        if (!weatherError) {
                            float temp = weatherDoc["current"]["temperature_2m"];
                            int hum = weatherDoc["current"]["relative_humidity_2m"];
                            int wmoCode = weatherDoc["current"]["weather_code"];
                            
                            const char* condition = getWeatherCondition(wmoCode);
                            
                            // Write it to the display buffer
                            sprintf(liveWeatherString, "%s: %s %dC   HUMIDITY %d%%   ", officialName.c_str(), condition, (int)round(temp), hum);
                        }
                    }
                    http.end();
                } else {
                    sprintf(liveWeatherString, "CITY %s IN %s NOT FOUND   ", normalizeCityName(targetCityName).c_str(), targetCountryCode.c_str());
                }
            } else {
                sprintf(liveWeatherString, "CITY %s NOT FOUND   ", normalizeCityName(targetCityName).c_str());
                http.end();
            }
        }
    }
}

// ==========================================
// 7. NON-BLOCKING ANIMATIONS
// ==========================================
bool animTimeWeather(int durationMs, const char* weatherMsg, bool init = false) {
    static unsigned long startMillis = 0;
    static unsigned long prevScroll = 0;
    static unsigned long prevBlink = 0;
    static int scrollX = 0;
    static bool blinkState = true;

    int screen1Start = getVirtualModuleStart(1, 0);

    if (init) {
        startMillis = millis();
        prevScroll = millis();
        prevBlink = millis();
        scrollX = VIRTUAL_COLS;
        blinkState = true;
        return false;
    }

    if (millis() - startMillis >= durationMs) {
        return true; 
    }

    bool needsRedraw = false;

    if (millis() - prevBlink >= 500) {
        prevBlink = millis();
        blinkState = !blinkState;
        needsRedraw = true;
    }

    if (millis() - prevScroll >= 30) {
        prevScroll = millis();
        scrollX--;
        int len = strlen(weatherMsg);
        if (scrollX < screen1Start - (len * 6)) {
            scrollX = VIRTUAL_COLS; 
        }
        needsRedraw = true;
    }

    if (needsRedraw) {
        clearBuffer();

        // --- SCREEN 0: Time ---
        struct tm timeinfo;
        if(getLocalTime(&timeinfo)) {
            char timeStr[16];
            strftime(timeStr, sizeof(timeStr), "%d %b %H %M", &timeinfo);
            
            for(int i=0; i<12; i++) {
                char c = timeStr[i];
                if(c >= 'a' && c <= 'z') c -= 32; 
                drawModuleChar(0, i + 2, c);
            }
            if (blinkState) drawModuleChar(0, 9 + 2, ':');
        }

        // --- SCREEN 1: Weather Scroll ---
        int len = strlen(weatherMsg);
        int x = scrollX;
        for(int i=0; i<len; i++) {
            char c = weatherMsg[i];

            // 1. Force lowercase to uppercase
            if (c >= 'a' && c <= 'z') {
                c -= 32; 
            }

            // 2. Handle Custom '%'
            if (c == '%') {
                for(int col=0; col<4; col++) {
                    int vx = x + col;
                    if (vx >= screen1Start && vx < VIRTUAL_COLS) {
                         if(col==0) { setPixel(vx, 1, 1); setPixel(vx, 6, 1); }
                         if(col==1) setPixel(vx, 3, 1);
                         if(col==2) setPixel(vx, 4, 1);
                         if(col==3) { setPixel(vx, 1, 1); setPixel(vx, 6, 1); }
                    }
                }
                x += 5;
                continue;
            }

            // 3. Handle Custom ':'
            if (c == ':') {
                int vx = x;
                if (vx >= screen1Start && vx < VIRTUAL_COLS) {
                    setPixel(vx, 2, 1); setPixel(vx, 5, 1); // Draw the two dots
                }
                x += 3; // Colons are narrow, so we save space
                continue;
            }

            // 4. Handle Custom '.'
            if (c == '.') {
                int vx = x;
                if (vx >= screen1Start && vx < VIRTUAL_COLS) {
                    setPixel(vx, 7, 1); // Draw a single dot at the bottom
                }
                x += 3; // Dots are narrow
                continue;
            }

            // 5. Normal Characters
            uint8_t idx = getCharIdxText(c);
            for(int col=0; col<5; col++) {
                int vx = x + col;
                if (vx >= screen1Start && vx < VIRTUAL_COLS) {
                    uint8_t data = FONT_TEXT[idx][col];
                    for(int r=0; r<8; r++) if(data & (1<<r)) setPixel(vx, r, 1);
                }
            }
            x += 6; 
        }
    }
    return false;
}

bool animSparkle(int speed, bool init = false) {
    static unsigned long prevMillis = 0;
    static int count = 0;
    if (init) { count = 0; prevMillis = millis(); return false; }
    if (millis() - prevMillis >= speed) {
        prevMillis = millis();
        setPixel(random(0, VIRTUAL_COLS), random(0, ROWS), 0);
        count++;
        if (count >= 800) { clearBuffer(); return true; }
    }
    return false;
}

bool animScanner(int loops, bool init = false) {
    static unsigned long prevMillis = 0;
    static int currentLoop = 0;
    static int i = 0, dir = 1; 
    if (init) { currentLoop = 0; i = 0; dir = 1; prevMillis = millis(); return false; }
    if (millis() - prevMillis >= 5) {
        prevMillis = millis();
        clearBuffer();
        for(int w=0; w<4; w++) {
            for(int h=0; h<8; h++) setPixel(i+w, h, 1);
        }
        i += (dir * 2);
        if (dir == 1 && i >= VIRTUAL_COLS - 4) { dir = -1; i = VIRTUAL_COLS - 4; } 
        else if (dir == -1 && i <= 0) { dir = 1; i = 0; currentLoop++;
            if (currentLoop >= loops) { clearBuffer(); return true; }
        }
    }
    return false;
}

bool animScroll(const char* txt, bool init = false) {
    static unsigned long prevMillis = 0;
    static int x = 0;
    int len = strlen(txt);
    if (init) { x = VIRTUAL_COLS; prevMillis = millis(); return false; }
    if (millis() - prevMillis >= 20) {
        prevMillis = millis();
        clearBuffer();
        drawTextAt(x, txt);
        x--;
        if (x <= -(len * 6)) return true;
    }
    return false;
}

bool animTypewriter(const char* msg, bool init = false) {
    static unsigned long prevMillis = 0;
    static int charIndex = 0;
    static bool isWaiting = false;
    int len = strlen(msg);
    if (init) { clearBuffer(); charIndex = 0; isWaiting = false; prevMillis = millis(); return false; }
    if (isWaiting) {
        if (millis() - prevMillis >= 3000) return true;
    } else {
        if (millis() - prevMillis >= 80) {
            prevMillis = millis();
            int startX = (len * 6 < VIRTUAL_COLS) ? (VIRTUAL_COLS - (len * 6)) / 2 : 0;
            char buf[2] = {msg[charIndex], '\0'};
            drawTextAt(startX + (charIndex * 6), buf);
            charIndex++;
            if (charIndex >= len) isWaiting = true; 
        }
    }
    return false;
}

bool displayStaticScreen(bool init = false) {
    static unsigned long prevMillis = 0;
    if (init) {
        clearBuffer();
        drawTemp(0, 0, 24.5);             
        drawIcon(0, 5, ICON_SUN_MOD);     
        drawTemp(0, 9, 18.2);             
        drawIcon(0, 14, ICON_CLOUD_MOD);

        drawTemp(1, 0, 12.0);
        drawIcon(1, 5, ICON_RAIN_MOD);
        drawTemp(1, 9, -5.5);
        drawIcon(1, 14, ICON_THUNDER_MOD);
        
        prevMillis = millis();
        return false;
    }
    if (millis() - prevMillis >= 5000) return true;
    return false;
}

// ==========================================
// 8. SETUP & ISR
// ==========================================
void IRAM_ATTR onTimer() {
    static uint8_t currentRow = 0;

    OE_HIGH(); 
    uint8_t addr = 7 - currentRow;
    if (addr & 0x01) ADDR_A_HIGH(); else ADDR_A_LOW();
    if (addr & 0x02) ADDR_B_HIGH(); else ADDR_B_LOW();
    if (addr & 0x04) ADDR_C_HIGH(); else ADDR_C_LOW();

    for(volatile int k=0; k<5; k++) __asm__("nop");

    for (int8_t b = 0; b < BUFFER_BYTES; b++) {
        uint8_t data = displayBuffer[currentRow][b];
        for (int8_t i = 0; i < 8; i++) {
            CLK_LOW();
            if (data & (1 << i)) DATA_HIGH(); else DATA_LOW();
            for(volatile int k=0; k<4; k++) __asm__("nop"); 
            CLK_HIGH();
            for(volatile int k=0; k<4; k++) __asm__("nop"); 
        }
    }

    STR_HIGH();
    for(volatile int k=0; k<4; k++) __asm__("nop"); 
    STR_LOW();

    OE_LOW(); 
    if (BRIGHTNESS > 0) {
        long loops = BRIGHTNESS * 10; 
        while(loops--) __asm__("nop");
    }
    OE_HIGH(); 

    currentRow = (currentRow + 1) & 0x07;
}

void setup() {
    Serial.begin(115200);

    pinMode(DATA_PIN, OUTPUT); pinMode(CLK_PIN, OUTPUT);
    pinMode(STR_PIN, OUTPUT);  pinMode(OE_PIN, OUTPUT);
    pinMode(ADDR_A, OUTPUT);   pinMode(ADDR_B, OUTPUT);
    pinMode(ADDR_C, OUTPUT);   pinMode(G1_BLANK, OUTPUT);
    
    OE_HIGH(); 
    BLANK_HIGH(); 

    // USING STABLE HARDWARE TIMER
    timer = timerBegin(1000000); 
    timerAttachInterrupt(timer, &onTimer);
    timerAlarm(timer, REFRESH_INTERVAL_US, true, 0);

    clearBuffer();
    drawTextAt(20, "WIFI...");
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.waitForConnectResult() != WL_CONNECTED) {
        delay(1000);
        ESP.restart(); 
    }

    String ipStr = "IP: " + WiFi.localIP().toString();
    ipStr.toCharArray(ipString, 30);

    // --- TIME CONFIGURATION ---
    // Force ESP32 to only hit the NTP server every 12 hours.
    sntp_set_sync_interval(12 * 60 * 60 * 1000); 
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

    ArduinoOTA.setHostname("MatrixDisplay");
    ArduinoOTA.begin();
}

// ==========================================
// 9. MAIN LOOP (STATE MACHINE)
// ==========================================
enum AppState {
    STATE_BOOT_IP,
    STATE_CLOCK,
    STATE_TYPEWRITER,
    STATE_SPARKLE,
    STATE_SCANNER,
    STATE_STATIC,
    STATE_SCROLL
};

AppState currentState = STATE_BOOT_IP;
bool initNextAnim = true;

void loop() {
    ArduinoOTA.handle();

    switch (currentState) {
        case STATE_BOOT_IP:
            if (initNextAnim) { 
                animScroll(ipString, true); 
                initNextAnim = false; 
            }
            if (animScroll(ipString)) {
                // Fetch first weather update before starting the clock
                if (millis() - lastWeatherUpdate >= weatherUpdateInterval || lastWeatherUpdate == 0) {
                    fetchWeather();
                    lastWeatherUpdate = millis();
                }
                currentState = STATE_CLOCK;
                initNextAnim = true;
            }
            break;

        case STATE_CLOCK:
            if (initNextAnim) { 
                // Pass in the live global string!
                animTimeWeather(15000, liveWeatherString, true); 
                initNextAnim = false; 
            }
            if (animTimeWeather(15000, liveWeatherString)) {
                currentState = STATE_TYPEWRITER;
                initNextAnim = true;
            }
            break;

        case STATE_TYPEWRITER:
            if (initNextAnim) { 
                animTypewriter("ESP32 OK", true); 
                initNextAnim = false; 
            }
            if (animTypewriter("ESP32 OK")) {
                currentState = STATE_SPARKLE;
                initNextAnim = true;
            }
            break;

        case STATE_SPARKLE:
            if (initNextAnim) { 
                animSparkle(2, true); 
                initNextAnim = false; 
            }
            if (animSparkle(2)) {
                currentState = STATE_SCANNER;
                initNextAnim = true;
            }
            break;

        case STATE_SCANNER:
            if (initNextAnim) { 
                animScanner(2, true); 
                initNextAnim = false; 
            }
            if (animScanner(2)) {
                currentState = STATE_STATIC;
                initNextAnim = true;
            }
            break;

        case STATE_STATIC:
            if (initNextAnim) { 
                displayStaticScreen(true); 
                initNextAnim = false; 
            }
            if (displayStaticScreen()) {
                currentState = STATE_SCROLL;
                initNextAnim = true;
            }
            break;

        case STATE_SCROLL:
            if (initNextAnim) { 
                animScroll("DUAL SCREEN CONFIGURATION...", true); 
                initNextAnim = false; 
            }
            if (animScroll("DUAL SCREEN CONFIGURATION...")) {
                // Fetch background weather before returning to the clock
                if (millis() - lastWeatherUpdate >= weatherUpdateInterval || lastWeatherUpdate == 0) {
                    fetchWeather();
                    lastWeatherUpdate = millis();
                }
                currentState = STATE_CLOCK; 
                initNextAnim = true;
            }
            break;
    }
}