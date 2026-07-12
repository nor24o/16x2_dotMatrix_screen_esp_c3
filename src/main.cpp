#include <Arduino.h>
#include <soc/gpio_struct.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <time.h>
#include <esp_sntp.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <Preferences.h>
#include <SPI.h>

// ==========================================
// 1. CONFIGURATION, TIMING & STATE
// ==========================================
const bool ENABLE_GAPS = true;
int REFRESH_INTERVAL_US = 1500;

int BRIGHTNESS = 400;
int NIGHT_BRIGHTNESS = 40;
int NIGHT_START_HOUR = 22;
int NIGHT_END_HOUR = 7;
volatile int currentBrightness = BRIGHTNESS;

const char *WIFI_SSID = "Link2";
const char *WIFI_PASSWORD = "Horvat2017";

const char *ntpServer = "pool.ntp.org";
long gmtOffset_sec = 7200;
int daylightOffset_sec = 3600;

char ipString[30] = "WAITING FOR IP...";
char liveWeatherString[100] = "WAITING FOR WEATHER...   ";
char nightWeatherString[50] = "WAITING...";
volatile unsigned long lastWeatherUpdate = 0;
const unsigned long weatherUpdateInterval = 30 * 60 * 1000;

// --- CONFIGURABLE PLAYLIST SWAP INTERVALS (in ms) ---
int durationWeatherScroll = 15000;
int durationTypewriter = 6000;
int durationSparkle = 4000;
int durationScannerLoops = 3;
int durationTemps = 6000;
int durationCustomMsg = 10000;

// --- DYNAMIC DATA & PREFERENCES ---
Preferences preferences;
WiFiUDP udp;
const unsigned int localUdpPort = 8888;

String targetCityName = "Sauca";
String targetCountryCode = "RO";
String targetRegion = "Satu Mare";

float externalTemps[4] = {-999.0, -99.0, -999.0, -999.0};
float internetTemp = -999.0;           // Stores the temperature fetched from the internet
unsigned long lastLocalTempUpdate = 0; // Tracks the last time we got local UDP data

bool showClock = true;
bool showTypewriter = false;
bool showSparkle = false;
bool showScanner = false;
bool showTemps = true;
bool showMsg = false;
String customMessage = "HELLO";
unsigned long customMessageDuration = 10000;

enum AppState
{
  STATE_BOOT_IP,
  STATE_CLOCK,
  STATE_TYPEWRITER,
  STATE_SPARKLE,
  STATE_SCANNER,
  STATE_TEMPS,
  STATE_MESSAGE,
  STATE_WIPE_TO_TEMPS,
  STATE_SHOW_TEMPS,
  STATE_WIPE_TO_CLOCK,
  STATE_WIPE_TO_MSG,
  STATE_SHOW_MSG
};
AppState currentState = STATE_BOOT_IP;
bool initNextAnim = true;

// ==========================================
// 2. PIN DEFINITIONS & REG DIRECT ACCESS MACROS
// ==========================================
#define DATA_PIN 4
#define CLK_PIN 6
#define STR_PIN 3
#define OE_PIN 7
#define ADDR_A 2
#define ADDR_B 21
#define ADDR_C 0
#define G1_BLANK 5

#define DATA_HIGH() GPIO.out_w1ts.val = (1UL << DATA_PIN)
#define DATA_LOW() GPIO.out_w1tc.val = (1UL << DATA_PIN)
#define CLK_HIGH() GPIO.out_w1ts.val = (1UL << CLK_PIN)
#define CLK_LOW() GPIO.out_w1tc.val = (1UL << CLK_PIN)
#define STR_HIGH() GPIO.out_w1ts.val = (1UL << STR_PIN)
#define STR_LOW() GPIO.out_w1tc.val = (1UL << STR_PIN)
#define OE_HIGH() GPIO.out_w1ts.val = (1UL << OE_PIN)
#define OE_LOW() GPIO.out_w1tc.val = (1UL << OE_PIN)
#define ADDR_A_HIGH() GPIO.out_w1ts.val = (1UL << ADDR_A)
#define ADDR_A_LOW() GPIO.out_w1tc.val = (1UL << ADDR_A)
#define ADDR_B_HIGH() GPIO.out_w1ts.val = (1UL << ADDR_B)
#define ADDR_B_LOW() GPIO.out_w1tc.val = (1UL << ADDR_B)
#define ADDR_C_HIGH() GPIO.out_w1ts.val = (1UL << ADDR_C)
#define ADDR_C_LOW() GPIO.out_w1tc.val = (1UL << ADDR_C)
#define BLANK_HIGH() GPIO.out_w1ts.val = (1UL << G1_BLANK)
#define BLANK_LOW() GPIO.out_w1tc.val = (1UL << G1_BLANK)

// ==========================================
// 3. MATRIX HARDWARE MAP SETUP
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

// Bounded Layout Split: Animations start at Module 16 (Lower Row)
const int ANIM_START_X = 16 * VIRTUAL_MOD_WIDTH;

uint8_t drawBuffer[ROWS][BUFFER_BYTES];
volatile uint8_t displayBuffer[ROWS][BUFFER_BYTES];

hw_timer_t *timer = NULL;
TaskHandle_t displayTaskHandle = NULL;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

// Hardware SPI drives DATA/CLK for the shift registers instead of bit-banged
// GPIO. ESP32-C3 only exposes one general-purpose SPI controller (FSPI).
SPIClass dispSPI(FSPI);
const uint32_t MATRIX_SPI_HZ = 4000000; // raise if signal integrity allows

// ==========================================
// 4. FONTS
// ==========================================
const uint8_t FONT_BIG_DIGITS[][4] = {
    {0x7F, 0x41, 0x41, 0x7F}, {0x00, 0x42, 0x7F, 0x40}, {0x79, 0x49, 0x49, 0x4F}, {0x41, 0x49, 0x49, 0x7F}, {0x0F, 0x08, 0x08, 0x7F}, {0x4F, 0x49, 0x49, 0x79}, {0x7F, 0x49, 0x49, 0x79}, {0x01, 0x01, 0x01, 0x7F}, {0x7F, 0x49, 0x49, 0x7F}, {0x4F, 0x49, 0x49, 0x7F}, {0x00, 0x60, 0x60, 0x00}, {0x08, 0x08, 0x08, 0x08}, {0x00, 0x00, 0x00, 0x00}};
const uint8_t ICON_SUN_MOD[] = {0x42, 0x24, 0x3C, 0x24, 0x42};
const uint8_t ICON_CLOUD_MOD[] = {0x0C, 0x1E, 0x3F, 0x33, 0x1E};
const uint8_t ICON_RAIN_MOD[] = {0x0C, 0x1E, 0x3B, 0x33, 0x20};
const uint8_t ICON_THUNDER_MOD[] = {0x06, 0x1C, 0x38, 0x0E, 0x03};
const uint8_t FONT_TEXT[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, {0x7E, 0x11, 0x11, 0x11, 0x7E}, {0x7F, 0x49, 0x49, 0x49, 0x36}, {0x3E, 0x41, 0x41, 0x41, 0x22}, {0x7F, 0x41, 0x41, 0x22, 0x1C}, {0x7F, 0x49, 0x49, 0x49, 0x41}, {0x7F, 0x09, 0x09, 0x09, 0x01}, {0x3E, 0x41, 0x49, 0x49, 0x7A}, {0x7F, 0x08, 0x08, 0x08, 0x7F}, {0x00, 0x41, 0x7F, 0x41, 0x00}, {0x20, 0x40, 0x41, 0x3F, 0x01}, {0x7F, 0x08, 0x14, 0x22, 0x41}, {0x7F, 0x40, 0x40, 0x40, 0x40}, {0x7F, 0x02, 0x0C, 0x02, 0x7F}, {0x7F, 0x04, 0x08, 0x10, 0x7F}, {0x3E, 0x41, 0x41, 0x41, 0x3E}, {0x7F, 0x09, 0x09, 0x09, 0x06}, {0x3E, 0x41, 0x51, 0x21, 0x5E}, {0x7F, 0x09, 0x19, 0x29, 0x46}, {0x46, 0x49, 0x49, 0x49, 0x31}, {0x01, 0x01, 0x7F, 0x01, 0x01}, {0x3F, 0x40, 0x40, 0x40, 0x3F}, {0x1F, 0x20, 0x40, 0x20, 0x1F}, {0x3F, 0x40, 0x38, 0x40, 0x3F}, {0x63, 0x14, 0x08, 0x14, 0x63}, {0x07, 0x08, 0x70, 0x08, 0x07}, {0x61, 0x51, 0x49, 0x45, 0x43}, {0x3E, 0x41, 0x41, 0x41, 0x3E}, {0x00, 0x42, 0x7F, 0x40, 0x00}, {0x42, 0x61, 0x51, 0x49, 0x46}, {0x21, 0x41, 0x45, 0x4B, 0x31}, {0x18, 0x14, 0x12, 0x7F, 0x10}, {0x27, 0x45, 0x45, 0x45, 0x39}, {0x3C, 0x4A, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03}, {0x36, 0x49, 0x49, 0x49, 0x36}, {0x06, 0x49, 0x49, 0x29, 0x1E}};

// ==========================================
// 5. GRAPHICS ENGINE & VIEWPORT HELPERS
// ==========================================
void clearBuffer() { memset((void *)drawBuffer, 0, sizeof(drawBuffer)); }

// Clears only the Lower Row animation canvas space
void clearAnimZone()
{
  for (int y = 0; y < ROWS; y++)
  {
    for (int x = ANIM_START_X; x < VIRTUAL_COLS; x++)
    {
      int physX = (!ENABLE_GAPS) ? x : ((x / VIRTUAL_MOD_WIDTH) * MODULE_WIDTH) + (x % VIRTUAL_MOD_WIDTH);
      if (x % VIRTUAL_MOD_WIDTH >= MODULE_WIDTH)
        continue;
      int byteIdx = physX / 8;
      int bitIdx = physX % 8;
      if (byteIdx < BUFFER_BYTES)
      {
        drawBuffer[y][byteIdx] &= ~(1 << bitIdx);
      }
    }
  }
}

int getPhysicalX(int vx)
{
  if (!ENABLE_GAPS)
    return vx;
  int modIdx = vx / VIRTUAL_MOD_WIDTH;
  int offset = vx % VIRTUAL_MOD_WIDTH;
  if (offset >= MODULE_WIDTH)
    return -1;
  return (modIdx * MODULE_WIDTH) + offset;
}

void setPixel(int virtualX, int y, uint8_t state)
{
  if (virtualX < 0 || virtualX >= VIRTUAL_COLS || y < 0 || y >= ROWS)
    return;
  int physX = getPhysicalX(virtualX);
  if (physX == -1)
    return;
  int byteIdx = physX / 8;
  int bitIdx = physX % 8;
  if (byteIdx < BUFFER_BYTES)
  {
    if (state)
      drawBuffer[y][byteIdx] |= (1 << bitIdx);
    else
      drawBuffer[y][byteIdx] &= ~(1 << bitIdx);
  }
}

int getVirtualModuleStart(uint8_t screen, uint8_t mod) { return ((screen * MODS_PER_SCREEN) + mod) * VIRTUAL_MOD_WIDTH; }

uint8_t getCharIdxText(char c)
{
  if (c >= 'A' && c <= 'Z')
    return c - 'A' + 1;
  if (c >= '0' && c <= '9')
    return c - '0' + 27;
  if (c == '%')
    return 36;
  if (c == '.')
    return 0;
  return 0;
}

void drawChar(uint8_t screen, uint8_t mod, char c)
{
  int startX = getVirtualModuleStart(screen, mod);
  uint8_t idx = 12;
  if (c >= '0' && c <= '9')
    idx = c - '0';
  if (c == '.')
    idx = 10;
  if (c == '-')
    idx = 11;
  for (uint8_t col = 0; col < 4; col++)
  {
    uint8_t data = FONT_BIG_DIGITS[idx][col];
    for (uint8_t r = 0; r < 8; r++)
      if (data & (1 << r))
        setPixel(startX + col, r, 1);
  }
}

void drawModuleChar(uint8_t screen, uint8_t mod, char c)
{
  int startX = getVirtualModuleStart(screen, mod);
  if (c >= 'a' && c <= 'z')
    c -= 32;
  if (c == ':')
  {
    setPixel(startX + 1, 2, 1);
    setPixel(startX + 1, 5, 1);
    setPixel(startX + 2, 2, 1);
    setPixel(startX + 2, 5, 1);
    return;
  }
  if (c == '.')
  {
    setPixel(startX + 1, 7, 1);
    setPixel(startX + 2, 7, 1);
    return;
  }
  if (c == '%')
  {
    setPixel(startX, 1, 1);
    setPixel(startX + 3, 1, 1);
    setPixel(startX + 1, 3, 1);
    setPixel(startX + 2, 4, 1);
    setPixel(startX, 6, 1);
    setPixel(startX + 3, 6, 1);
    return;
  }
  if (c == '-')
  {
    setPixel(startX + 1, 4, 1);
    setPixel(startX + 2, 4, 1);
    setPixel(startX + 3, 4, 1);
    return;
  }
  uint8_t idx = getCharIdxText(c);
  if (idx == 0 && c != ' ')
    return;
  for (int col = 0; col < 5; col++)
  {
    uint8_t data = FONT_TEXT[idx][col];
    for (int r = 0; r < 8; r++)
      if (data & (1 << r))
        setPixel(startX + col, r, 1);
  }
}

void drawIcon(uint8_t screen, uint8_t mod, const uint8_t *icon)
{
  int startX = getVirtualModuleStart(screen, mod);
  for (uint8_t col = 0; col < 5; col++)
  {
    uint8_t data = icon[col];
    for (uint8_t r = 0; r < 8; r++)
      if (data & (1 << r))
        setPixel(startX + col, r, 1);
  }
}

void drawTemp(uint8_t screen, uint8_t startMod, float temp)
{
  int val = (int)(abs(temp) * 10);
  int d3 = val % 10;
  int d2 = (val / 10) % 10;
  int d1 = (val / 100) % 10;

  if (temp <= -10.0)
  {
    drawChar(screen, startMod, '-');
    drawChar(screen, startMod + 1, '0' + d1);
    drawChar(screen, startMod + 2, '0' + d2);
    drawChar(screen, startMod + 3, ' ');
  }
  else if (temp < 0)
  {
    drawChar(screen, startMod, '-');
    drawChar(screen, startMod + 1, '0' + d2);
    drawChar(screen, startMod + 2, '.');
    drawChar(screen, startMod + 3, '0' + d3);
  }
  else
  {
    if (temp >= 10.0)
      drawChar(screen, startMod, '0' + d1);
    else
      drawChar(screen, startMod, ' ');
    drawChar(screen, startMod + 1, '0' + d2);
    drawChar(screen, startMod + 2, '.');
    drawChar(screen, startMod + 3, '0' + d3);
  }
}

// Global Core Layering - Overwrites Upper Row memory directly inside show()
void drawPermanentClock()
{
  static int lastMinute = -1;
  static char cachedTimeStr[16] = "            ";
  static unsigned long prevBlink = 0;
  static bool blinkState = true;

  if (millis() - prevBlink >= 500)
  {
    prevBlink = millis();
    blinkState = !blinkState;
  }

  struct tm timeinfo;
  if (getLocalTime(&timeinfo))
  {
    if (timeinfo.tm_min != lastMinute)
    {
      lastMinute = timeinfo.tm_min;
      strftime(cachedTimeStr, sizeof(cachedTimeStr), "%d %b %H %M", &timeinfo);
    }
  }

  // Clear Upper Row space cleanly before writing time frames
  for (int y = 0; y < ROWS; y++)
  {
    for (int x = 0; x < ANIM_START_X; x++)
    {
      int physX = getPhysicalX(x);
      if (physX == -1)
        continue;
      int byteIdx = physX / 8;
      int bitIdx = physX % 8;
      drawBuffer[y][byteIdx] &= ~(1 << bitIdx);
    }
  }

  // Render centered time string on Upper Row (Modules 0-15)
  for (int i = 0; i < 12; i++)
  {
    drawModuleChar(0, i + 2, cachedTimeStr[i]);
  }
  drawModuleChar(0, 9 + 2, blinkState ? ':' : ' ');
}

void show()
{
  drawPermanentClock(); // Intercept and inject clock to Upper Row
  portENTER_CRITICAL(&timerMux);
  memcpy((void *)displayBuffer, (void *)drawBuffer, sizeof(displayBuffer));
  portEXIT_CRITICAL(&timerMux);
}

String normalizeCityName(String name)
{
  name.replace("ă", "A");
  name.replace("Ă", "A");
  name.replace("â", "A");
  name.replace("Â", "A");
  name.replace("î", "I");
  name.replace("Î", "I");
  name.replace("ș", "S");
  name.replace("Ș", "S");
  name.replace("ş", "S");
  name.replace("Ş", "S");
  name.replace("ț", "T");
  name.replace("Ț", "T");
  name.replace("ţ", "T");
  name.replace("Ţ", "T");
  name.toUpperCase();
  String cleanName = "";
  for (int i = 0; i < name.length(); i++)
  {
    char c = name[i];
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '-')
      cleanName += c;
  }
  return cleanName;
}

const char *getWeatherCondition(int code)
{
  if (code == 0)
    return "CLEAR";
  if (code == 1 || code == 2)
    return "PARTLY CLOUDY";
  if (code == 3)
    return "OVERCAST";
  if (code >= 45 && code <= 48)
    return "FOGGY";
  if (code >= 51 && code <= 67)
    return "RAIN";
  if (code >= 71 && code <= 77)
    return "SNOW";
  if (code >= 80 && code <= 82)
    return "SHOWERS";
  if (code >= 95 && code <= 99)
    return "THUNDERSTORM";
  return "UNKNOWN";
}

const char *getShortWeatherCondition(int code)
{
  if (code == 0)
    return "CLR";
  if (code == 1 || code == 2)
    return "P.CLD";
  if (code == 3)
    return "OCST";
  if (code >= 45 && code <= 48)
    return "FOG";
  if (code >= 51 && code <= 67)
    return "RAIN";
  if (code >= 71 && code <= 77)
    return "SNOW";
  if (code >= 80 && code <= 82)
    return "SHWR";
  if (code >= 95 && code <= 99)
    return "STORM";
  return "UNK";
}

bool isNightTime()
{
  struct tm timeinfo;
  if (getLocalTime(&timeinfo))
  {
    if (NIGHT_START_HOUR > NIGHT_END_HOUR)
    {
      if (timeinfo.tm_hour >= NIGHT_START_HOUR || timeinfo.tm_hour < NIGHT_END_HOUR)
        return true;
    }
    else if (NIGHT_START_HOUR < NIGHT_END_HOUR)
    {
      if (timeinfo.tm_hour >= NIGHT_START_HOUR && timeinfo.tm_hour < NIGHT_END_HOUR)
        return true;
    }
  }
  return false;
}

// ==========================================
// 6. NETWORK & DATA FUNCTIONS
// ==========================================
void fetchWeather()
{
  if (WiFi.status() == WL_CONNECTED)
  {
    HTTPClient http;
    String geoUrl = "http://geocoding-api.open-meteo.com/v1/search?name=" + targetCityName + "&count=10";
    http.begin(geoUrl);
    int geoResponseCode = http.GET();

    if (geoResponseCode == 200)
    {
      String geoPayload = http.getString();
      JsonDocument geoDoc;
      DeserializationError geoError = deserializeJson(geoDoc, geoPayload);

      if (!geoError && geoDoc["results"].size() > 0)
      {
        float lat = 0.0;
        float lon = 0.0;
        String officialName = "";
        bool matchFound = false;
        JsonArray results = geoDoc["results"];
        for (JsonObject result : results)
        {
          String cCode = result["country_code"].as<String>();
          String admin1 = result["admin1"].as<String>();
          if (cCode == targetCountryCode)
          {
            if (targetRegion == "" || admin1.indexOf(targetRegion) >= 0)
            {
              lat = result["latitude"];
              lon = result["longitude"];
              officialName = normalizeCityName(result["name"].as<String>());
              matchFound = true;
              break;
            }
          }
        }
        http.end();

        if (matchFound)
        {
          String weatherUrl = "http://api.open-meteo.com/v1/forecast?latitude=" + String(lat, 4) + "&longitude=" + String(lon, 4) + "&current=temperature_2m,relative_humidity_2m,weather_code";
          http.begin(weatherUrl);
          int weatherResponseCode = http.GET();

          if (weatherResponseCode == 200)
          {
            String weatherPayload = http.getString();
            JsonDocument weatherDoc;
            DeserializationError weatherError = deserializeJson(weatherDoc, weatherPayload);
            if (!weatherError)
            {
              // Actually grab the internet temperature from the JSON
              internetTemp = weatherDoc["current"]["temperature_2m"];

              // Use local temp for the weather scroll IF it's fresh and valid; otherwise fallback to internet
              float temp = internetTemp;
              if (lastLocalTempUpdate != 0 && (millis() - lastLocalTempUpdate < 300000) && externalTemps[1] < 99.0)
              {
                temp = externalTemps[1];
              }

              int hum = weatherDoc["current"]["relative_humidity_2m"];
              int wmoCode = weatherDoc["current"]["weather_code"];
              const char *condition = getWeatherCondition(wmoCode);
              sprintf(liveWeatherString, "%s: %s %dC   HUMIDITY %d%%   ", officialName.c_str(), condition, (int)round(temp), hum);
              String shortName = officialName.substring(0, 2);
              const char *shortCond = getShortWeatherCondition(wmoCode);
              sprintf(nightWeatherString, "%s:%s T%.1fC", shortName.c_str(), shortCond, temp);
            }
          }
          http.end();
        }
        else
        {
          sprintf(liveWeatherString, "CITY %s IN %s NOT FOUND   ", normalizeCityName(targetCityName).c_str(), targetCountryCode.c_str());
          sprintf(nightWeatherString, "NOT FOUND");
        }
      }
      else
      {
        sprintf(liveWeatherString, "CITY %s NOT FOUND   ", normalizeCityName(targetCityName).c_str());
        sprintf(nightWeatherString, "NOT FOUND");
        http.end();
      }
    }
  }
}

void weatherUpdateTask(void *pvParameters)
{
  for (;;)
  {
    if (WiFi.status() == WL_CONNECTED)
    {
      if (millis() - lastWeatherUpdate >= weatherUpdateInterval || lastWeatherUpdate == 0)
      {
        fetchWeather();
        lastWeatherUpdate = millis();
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

// ==========================================
// 6.5. UDP COMMAND HANDLER
// ==========================================
void handleUDP()
{
  int packetSize = udp.parsePacket();
  if (packetSize)
  {
    char incomingPacket[255];
    int len = udp.read(incomingPacket, 255);
    if (len > 0)
      incomingPacket[len] = 0;

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, incomingPacket);

    if (!error)
    {
      const char *cmd = doc["cmd"];

      if (cmd && strcmp(cmd, "config") == 0)
      {
        if (!doc["bright"].isNull())
        {
          BRIGHTNESS = constrain(doc["bright"].as<int>(), 0, 1000);
          preferences.putInt("bright", BRIGHTNESS);
        }
        if (!doc["nbright"].isNull())
        {
          NIGHT_BRIGHTNESS = constrain(doc["nbright"].as<int>(), 0, 1000);
          preferences.putInt("nbright", NIGHT_BRIGHTNESS);
        }
        if (!doc["nstart"].isNull())
        {
          NIGHT_START_HOUR = constrain(doc["nstart"].as<int>(), 0, 23);
          preferences.putInt("nstart", NIGHT_START_HOUR);
        }
        if (!doc["nend"].isNull())
        {
          NIGHT_END_HOUR = constrain(doc["nend"].as<int>(), 0, 23);
          preferences.putInt("nend", NIGHT_END_HOUR);
        }

        // Target Dynamic Interval Modifiers
        if (!doc["t_scroll"].isNull())
        {
          durationWeatherScroll = doc["t_scroll"].as<int>();
          preferences.putInt("t_scroll", durationWeatherScroll);
        }
        if (!doc["t_type"].isNull())
        {
          durationTypewriter = doc["t_type"].as<int>();
          preferences.putInt("t_type", durationTypewriter);
        }
        if (!doc["t_sparkle"].isNull())
        {
          durationSparkle = doc["t_sparkle"].as<int>();
          preferences.putInt("t_sparkle", durationSparkle);
        }
        if (!doc["t_scan"].isNull())
        {
          durationScannerLoops = doc["t_scan"].as<int>();
          preferences.putInt("t_scan", durationScannerLoops);
        }
        if (!doc["t_temps"].isNull())
        {
          durationTemps = doc["t_temps"].as<int>();
          preferences.putInt("t_temps", durationTemps);
        }

        if (!doc["gmtoff"].isNull())
        {
          gmtOffset_sec = constrain(doc["gmtoff"].as<long>(), -43200, 50400);
          preferences.putLong("gmtoff", gmtOffset_sec);
          configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
        }
        if (!doc["city"].isNull())
        {
          targetCityName = doc["city"].as<String>().substring(0, 30);
          preferences.putString("city", targetCityName);
          lastWeatherUpdate = 0;
        }
        if (!doc["country"].isNull())
        {
          targetCountryCode = doc["country"].as<String>().substring(0, 3);
          preferences.putString("country", targetCountryCode);
          lastWeatherUpdate = 0;
        }
        if (!doc["region"].isNull())
        {
          targetRegion = doc["region"].as<String>().substring(0, 30);
          preferences.putString("region", targetRegion);
          lastWeatherUpdate = 0;
        }
      }
      else if (cmd && strcmp(cmd, "playlist") == 0)
      {
        if (!doc["clock"].isNull())
        {
          showClock = doc["clock"];
          preferences.putBool("sClk", showClock);
        }
        if (!doc["typewriter"].isNull())
        {
          showTypewriter = doc["typewriter"];
          preferences.putBool("sTyp", showTypewriter);
        }
        if (!doc["sparkle"].isNull())
        {
          showSparkle = doc["sparkle"];
          preferences.putBool("sSprk", showSparkle);
        }
        if (!doc["scanner"].isNull())
        {
          showScanner = doc["scanner"];
          preferences.putBool("sScan", showScanner);
        }
        if (!doc["temps"].isNull())
        {
          showTemps = doc["temps"];
          preferences.putBool("sTmp", showTemps);
        }
        if (!doc["msg"].isNull())
        {
          showMsg = doc["msg"];
          preferences.putBool("sMsg", showMsg);
        }

        if (!showClock && !showTypewriter && !showSparkle && !showScanner && !showTemps && !showMsg)
          showClock = true;

        JsonDocument replyDoc;
        replyDoc["status"] = "PLAYLIST_UPDATED";
        udp.beginPacket(udp.remoteIP(), udp.remotePort());
        serializeJson(replyDoc, udp);
        udp.endPacket();
      }
      else if (cmd && strcmp(cmd, "message") == 0)
      {
        if (!doc["text"].isNull())
          customMessage = doc["text"].as<String>().substring(0, 100);
        customMessageDuration = (!doc["time"].isNull()) ? constrain(doc["time"].as<long>(), 1000, 300000) : 10000;

        currentState = STATE_WIPE_TO_MSG;
        initNextAnim = true;

        JsonDocument replyDoc;
        replyDoc["status"] = "MSG_RECEIVED";
        udp.beginPacket(udp.remoteIP(), udp.remotePort());
        serializeJson(replyDoc, udp);
        udp.endPacket();
      }
      else if (cmd && strcmp(cmd, "temps") == 0)
      {
        lastLocalTempUpdate = millis();

        if (!doc["t1"].isNull())
          externalTemps[0] = constrain(doc["t1"].as<float>(), -99.9, 99.9);
        if (!doc["t2"].isNull())
          externalTemps[1] = constrain(doc["t2"].as<float>(), -99.9, 99.9);
        if (!doc["t3"].isNull())
          externalTemps[2] = constrain(doc["t3"].as<float>(), -99.9, 99.9);
        if (!doc["t4"].isNull())
          externalTemps[3] = constrain(doc["t4"].as<float>(), -99.9, 99.9);

        String nStr = nightWeatherString;
        int tIdx = nStr.indexOf(" T");
        if (tIdx != -1)
        {
          String newN = nStr.substring(0, tIdx + 2) + String(externalTemps[1], 1) + "C";
          newN.toCharArray(nightWeatherString, 50);
        }

        String lStr = liveWeatherString;
        int cIdx2 = lStr.indexOf("C   HUMIDITY");
        if (cIdx2 != -1)
        {
          int spaceIdx = lStr.lastIndexOf(" ", cIdx2);
          if (spaceIdx != -1)
          {
            String newL = lStr.substring(0, spaceIdx + 1) + String(externalTemps[1], 1) + lStr.substring(cIdx2);
            newL.toCharArray(liveWeatherString, 100);
          }
        }

        currentState = STATE_WIPE_TO_TEMPS;
        initNextAnim = true;

        JsonDocument replyDoc;
        replyDoc["status"] = "TEMPS_RECEIVED";
        udp.beginPacket(udp.remoteIP(), udp.remotePort());
        serializeJson(replyDoc, udp);
        udp.endPacket();
      }
    }
  }
}

// ==========================================
// 7. LOWER ROW TARGETED ANIMATIONS
// ==========================================

bool animWeatherScroll(int durationMs, const char *weatherMsg, bool init = false)
{
  static unsigned long startMillis = 0;
  static unsigned long prevScroll = 0;
  static int scrollX = 0;
  bool nightMode = isNightTime();

  if (init)
  {
    startMillis = millis();
    prevScroll = millis();
    scrollX = VIRTUAL_COLS;
    return false;
  }

  if (millis() - startMillis >= durationMs)
    return true;

  if (!nightMode && (millis() - prevScroll >= 30))
  {
    prevScroll = millis();
    scrollX--;
    int len = strlen(weatherMsg);
    if (scrollX < ANIM_START_X - (len * VIRTUAL_MOD_WIDTH))
    {
      scrollX = VIRTUAL_COLS;
    }

    clearAnimZone();
    int x = scrollX;
    for (int i = 0; i < len; i++)
    {
      char c = weatherMsg[i];
      if (c >= 'a' && c <= 'z')
        c -= 32;

      if (x >= ANIM_START_X - VIRTUAL_MOD_WIDTH && x < VIRTUAL_COLS)
      {
        if (c == '.')
        {
          if (x + 1 >= ANIM_START_X)
            setPixel(x + 1, 7, 1);
          if (x + 2 >= ANIM_START_X)
            setPixel(x + 2, 7, 1);
        }
        else if (c == ':')
        {
          if (x + 1 >= ANIM_START_X)
          {
            setPixel(x + 1, 2, 1);
            setPixel(x + 1, 5, 1);
          }
          if (x + 2 >= ANIM_START_X)
          {
            setPixel(x + 2, 2, 1);
            setPixel(x + 2, 5, 1);
          }
        }
        else
        {
          uint8_t idx = getCharIdxText(c);
          for (int col = 0; col < 5; col++)
          {
            int vx = x + col;
            if (vx >= ANIM_START_X && vx < VIRTUAL_COLS)
            {
              uint8_t data = FONT_TEXT[idx][col];
              for (int r = 0; r < 8; r++)
                if (data & (1 << r))
                  setPixel(vx, r, 1);
            }
          }
        }
      }
      x += VIRTUAL_MOD_WIDTH;
    }
    show();
  }
  else if (nightMode)
  {
    clearAnimZone();
    int len = strlen(nightWeatherString);
    for (int i = 0; i < len && i < 16; i++)
    {
      drawModuleChar(1, i, nightWeatherString[i]); // Maps to physical Lower Row
    }
    show();
  }
  return false;
}

bool animSparkle(int speed, bool init = false)
{
  static unsigned long prevMillis = 0;
  static int count = 0;
  if (init)
  {
    count = 0;
    prevMillis = millis();
    return false;
  }
  if (millis() - prevMillis >= speed)
  {
    prevMillis = millis();
    setPixel(random(ANIM_START_X, VIRTUAL_COLS), random(0, ROWS), random(0, 2));
    show();
    count++;
    if (count >= 400)
    {
      clearAnimZone();
      show();
      return true;
    }
  }
  return false;
}

bool animScanner(int loops, bool init = false)
{
  static unsigned long prevMillis = 0;
  static int currentLoop = 0;
  static int i = 0, dir = 1;
  if (init)
  {
    currentLoop = 0;
    i = ANIM_START_X;
    dir = 1;
    prevMillis = millis();
    return false;
  }
  if (millis() - prevMillis >= 12)
  {
    prevMillis = millis();
    clearAnimZone();
    for (int w = 0; w < 4; w++)
      for (int h = 0; h < 8; h++)
        setPixel(i + w, h, 1);
    show();
    i += (dir * 2);
    if (dir == 1 && i >= VIRTUAL_COLS - 4)
    {
      dir = -1;
      i = VIRTUAL_COLS - 4;
    }
    else if (dir == -1 && i <= ANIM_START_X)
    {
      dir = 1;
      i = ANIM_START_X;
      currentLoop++;
      if (currentLoop >= loops)
      {
        clearAnimZone();
        show();
        return true;
      }
    }
  }
  return false;
}

bool animScroll(const char *txt, bool init = false)
{
  static unsigned long prevMillis = 0;
  static int x = 0;
  int len = strlen(txt);
  if (init)
  {
    x = VIRTUAL_COLS;
    prevMillis = millis();
    return false;
  }
  if (millis() - prevMillis >= 25)
  {
    prevMillis = millis();
    clearAnimZone();

    int currentX = x;
    for (int i = 0; i < len; i++)
    {
      char c = txt[i];
      if (c >= 'a' && c <= 'z')
        c -= 32;
      if (currentX >= ANIM_START_X - VIRTUAL_MOD_WIDTH && currentX < VIRTUAL_COLS)
      {
        uint8_t idx = getCharIdxText(c);
        for (int col = 0; col < 5; col++)
        {
          int vx = currentX + col;
          if (vx >= ANIM_START_X && vx < VIRTUAL_COLS)
          {
            uint8_t data = FONT_TEXT[idx][col];
            for (int r = 0; r < 8; r++)
              if (data & (1 << r))
                setPixel(vx, r, 1);
          }
        }
      }
      currentX += VIRTUAL_MOD_WIDTH;
    }
    show();
    x--;
    if (x <= ANIM_START_X - (len * VIRTUAL_MOD_WIDTH))
      return true;
  }
  return false;
}

bool animTypewriter(const char *msg, bool init = false)
{
  static unsigned long prevMillis = 0;
  static int charIndex = 0;
  static bool isWaiting = false;
  int len = strlen(msg);
  if (init)
  {
    clearAnimZone();
    show();
    charIndex = 0;
    isWaiting = false;
    prevMillis = millis();
    return false;
  }
  if (isWaiting)
  {
    if (millis() - prevMillis >= 3000)
      return true;
  }
  else
  {
    if (millis() - prevMillis >= 100)
    {
      prevMillis = millis();
      int startMod = (16 > len) ? 16 + (16 - len) / 2 : 16;
      int targetX = startMod * VIRTUAL_MOD_WIDTH;

      char c = msg[charIndex];
      uint8_t idx = getCharIdxText(c);
      int charX = targetX + (charIndex * VIRTUAL_MOD_WIDTH);

      for (int col = 0; col < 5; col++)
      {
        int vx = charX + col;
        if (vx >= ANIM_START_X && vx < VIRTUAL_COLS)
        {
          uint8_t data = FONT_TEXT[idx][col];
          for (int r = 0; r < 8; r++)
            if (data & (1 << r))
              setPixel(vx, r, 1);
        }
      }
      show();
      charIndex++;
      if (charIndex >= len)
        isWaiting = true;
    }
  }
  return false;
}

bool displayExternalTemps(int durationMs, bool init = false)
{
  static unsigned long startMillis = 0;
  static bool alternateBlock = false;
  if (init)
  {
    clearAnimZone();

    // Check if 5 minutes (300,000 ms) have passed, or if we never got data
    bool isStale = (lastLocalTempUpdate == 0) || (millis() - lastLocalTempUpdate > 300000);

    // Helper function: If stale or sensor reading is an error (>= 99), return internet temp
    auto getValidTemp = [&](float localT) {
      if (isStale || localT >= 99.0 || localT <= -99.0) {
        return internetTemp; 
      }
      return localT;
    };

    // Apply the fallback logic to whichever sensors are currently being displayed
    float tA = alternateBlock ? getValidTemp(externalTemps[2]) : getValidTemp(externalTemps[0]);
    float tB = alternateBlock ? getValidTemp(externalTemps[3]) : getValidTemp(externalTemps[1]);

    drawTemp(1, 0, tA); // Drawing to relative Module 0 on lower array
    drawIcon(1, 5, alternateBlock ? ICON_RAIN_MOD : ICON_SUN_MOD);
    drawTemp(1, 9, tB);
    drawIcon(1, 14, alternateBlock ? ICON_THUNDER_MOD : ICON_CLOUD_MOD);

    show();
    startMillis = millis();
    alternateBlock = !alternateBlock;
    return false;
  }
  if (millis() - startMillis >= durationMs)
    return true;
  return false;
}

bool animWipe(int speed, bool init = false)
{
  static unsigned long prevMillis = 0;
  static int col = 0;
  if (init)
  {
    col = ANIM_START_X;
    prevMillis = millis();
    return false;
  }
  if (millis() - prevMillis >= speed)
  {
    prevMillis = millis();
    for (int r = 0; r < ROWS; r++)
      setPixel(col, r, 0);
    show();
    col++;
    if (col >= VIRTUAL_COLS)
      return true;
  }
  return false;
}

bool animWipereverse(int speed, bool init = false)
{
  static unsigned long prevMillis = 0;
  static int col = 0;
  if (init)
  {
    col = VIRTUAL_COLS - 1;
    prevMillis = millis();
    return false;
  }
  if (millis() - prevMillis >= speed)
  {
    prevMillis = millis();
    for (int r = 0; r < ROWS; r++)
      setPixel(col, r, 0);
    show();
    col--;
    if (col < ANIM_START_X)
      return true;
  }
  return false;
}

bool displayCustomMessage(int durationMs, const char *msg, bool init = false)
{
  static unsigned long startMillis = 0;
  if (init)
  {
    startMillis = millis();
    animScroll(msg, true);
    return false;
  }
  if (animScroll(msg))
  {
    animScroll(msg, true);
  }
  if (millis() - startMillis >= durationMs)
    return true;
  return false;
}

// ==========================================
// 8. HIGH-PRIORITY HARDWARE TIMER ISR
// ==========================================
void IRAM_ATTR onTimer()
{
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  vTaskNotifyGiveFromISR(displayTaskHandle, &xHigherPriorityTaskWoken);
  if (xHigherPriorityTaskWoken)
    portYIELD_FROM_ISR();
}

// ==========================================
// 9. COOPERATIVE PREEMPTIVE DISPLAY REFRESH TASK
// ==========================================
void displayRefreshTask(void *pvParameters)
{
  uint8_t currentRow = 0;
  uint8_t rowBuf[BUFFER_BYTES];
  SPISettings matrixSPISettings(MATRIX_SPI_HZ, SPI_LSBFIRST, SPI_MODE0);

  for (;;)
  {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    // Only the buffer snapshot needs interrupts disabled - a handful of
    // instructions instead of the ~1ms+ this used to hold the CPU for.
    portENTER_CRITICAL(&timerMux);
    memcpy(rowBuf, (void *)displayBuffer[currentRow], BUFFER_BYTES);
    portEXIT_CRITICAL(&timerMux);

    OE_HIGH(); // blank while shifting/latching to avoid ghosting/streaking

    // Hardware-shifted: the SPI peripheral clocks these bytes out on its own,
    // so a WiFi interrupt landing mid-transfer just adds a few us of latency
    // instead of corrupting/stretching a bit-banged clock pulse.
    dispSPI.beginTransaction(matrixSPISettings);
    dispSPI.transfer(rowBuf, BUFFER_BYTES);
    dispSPI.endTransaction();

    uint8_t addr = 7 - currentRow;
    if (addr & 0x01)
      ADDR_A_HIGH();
    else
      ADDR_A_LOW();
    if (addr & 0x02)
      ADDR_B_HIGH();
    else
      ADDR_B_LOW();
    if (addr & 0x04)
      ADDR_C_HIGH();
    else
      ADDR_C_LOW();

    STR_HIGH();
    asm volatile("nop");
    asm volatile("nop");
    STR_LOW();
    delayMicroseconds(2);

    if (currentBrightness > 0)
    {
      OE_LOW();
      delayMicroseconds(currentBrightness);
      OE_HIGH();
    }
    currentRow = (currentRow + 1) & 0x07;
  }
}

// ==========================================
// 10. SETUP
// ==========================================
void setup()
{
  Serial.begin(115200);

  preferences.begin("matrix", false);
  BRIGHTNESS = constrain(preferences.getInt("bright", 400), 0, 1000);
  NIGHT_BRIGHTNESS = constrain(preferences.getInt("nbright", 40), 0, 1000);
  NIGHT_START_HOUR = constrain(preferences.getInt("nstart", 22), 0, 23);
  NIGHT_END_HOUR = constrain(preferences.getInt("nend", 7), 0, 23);

  // Initialize customizable intervals from persistent storage
  durationWeatherScroll = preferences.getInt("t_scroll", 15000);
  durationTypewriter = preferences.getInt("t_type", 6000);
  durationSparkle = preferences.getInt("t_sparkle", 4000);
  durationScannerLoops = preferences.getInt("t_scan", 3);
  durationTemps = preferences.getInt("t_temps", 6000);

  gmtOffset_sec = constrain(preferences.getLong("gmtoff", 7200), -43200, 50400);
  daylightOffset_sec = constrain(preferences.getInt("dstoff", 3600), 0, 7200);

  targetCityName = preferences.getString("city", "Sauca").substring(0, 30);
  targetCountryCode = preferences.getString("country", "RO").substring(0, 3);
  targetRegion = preferences.getString("region", "Satu Mare").substring(0, 30);

  showClock = preferences.getBool("sClk", true);
  showTypewriter = preferences.getBool("sTyp", true);
  showSparkle = preferences.getBool("sSprk", true);
  showScanner = preferences.getBool("sScan", true);
  showTemps = preferences.getBool("sTmp", true);
  showMsg = preferences.getBool("sMsg", false);
  customMessage = preferences.getString("cMsg", "HELLO");

  currentBrightness = BRIGHTNESS;

  pinMode(STR_PIN, OUTPUT);
  pinMode(OE_PIN, OUTPUT);
  pinMode(ADDR_A, OUTPUT);
  pinMode(ADDR_B, OUTPUT);
  pinMode(ADDR_C, OUTPUT);
  pinMode(G1_BLANK, OUTPUT);

  // DATA_PIN/CLK_PIN are now driven by hardware SPI (MOSI/SCK), not GPIO.
  // ESP32-C3's GPIO matrix lets any pin map to these regardless of number.
  dispSPI.begin(CLK_PIN, -1 /* MISO unused */, DATA_PIN, -1 /* CS unused */);

  OE_HIGH();
  BLANK_HIGH();
  clearBuffer();
  show();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  WiFi.setSleep(false);

  while (WiFi.waitForConnectResult() != WL_CONNECTED)
  {
    delay(1000);
    ESP.restart();
  }

  udp.begin(localUdpPort);
  String ipStr = "IP: " + WiFi.localIP().toString();
  ipStr.toCharArray(ipString, 30);

  sntp_set_sync_interval(12 * 60 * 60 * 1000);
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  ArduinoOTA.setHostname("matrix");
  ArduinoOTA.setPassword("admin");
  ArduinoOTA.begin();

  // Priority 24: must sit ABOVE the ESP-IDF WiFi task (~23) or the timer ISR's
  // wakeup can't actually preempt WiFi packet processing, which is what causes
  // irregular/jittery row timing on a single-core chip. This task blocks on
  // ulTaskNotifyTake almost the entire 1500us window, so it doesn't starve
  // WiFi/idle - it just gets first refusal on the CPU when its tick is due.
  xTaskCreate(displayRefreshTask, "DisplayTask", 4096, NULL, 24, &displayTaskHandle);

  timer = timerBegin(1000000);
  timerAttachInterrupt(timer, &onTimer);
  timerAlarm(timer, REFRESH_INTERVAL_US, true, 0);

  xTaskCreate(weatherUpdateTask, "WeatherTask", 8192, NULL, 1, NULL);
}

// ==========================================
// 11. MAIN CASCADING STATE MACHINE LOOP
// ==========================================
void loop()
{
  ArduinoOTA.handle();
  handleUDP();

  currentBrightness = isNightTime() ? NIGHT_BRIGHTNESS : BRIGHTNESS;

  switch (currentState)
  {
  case STATE_BOOT_IP:
    if (initNextAnim)
    {
      animScroll(ipString, true);
      initNextAnim = false;
    }
    if (animScroll(ipString))
    {
      currentState = STATE_CLOCK;
      initNextAnim = true;
    }
    break;

  case STATE_CLOCK:
    if (!showClock)
    {
      currentState = STATE_TYPEWRITER;
      initNextAnim = true;
      break;
    }
    if (initNextAnim)
    {
      animWeatherScroll(durationWeatherScroll, liveWeatherString, true);
      initNextAnim = false;
    }
    if (animWeatherScroll(durationWeatherScroll, liveWeatherString))
    {
      currentState = STATE_TYPEWRITER;
      initNextAnim = true;
    }
    break;

  case STATE_TYPEWRITER:
    if (!showTypewriter)
    {
      currentState = STATE_SPARKLE;
      initNextAnim = true;
      break;
    }
    if (initNextAnim)
    {
      animTypewriter("ESP32 OK", true);
      initNextAnim = false;
    }
    if (animTypewriter("ESP32 OK"))
    {
      currentState = STATE_SPARKLE;
      initNextAnim = true;
    }
    break;

  case STATE_SPARKLE:
    if (!showSparkle)
    {
      currentState = STATE_SCANNER;
      initNextAnim = true;
      break;
    }
    if (initNextAnim)
    {
      animSparkle(2, true);
      initNextAnim = false;
    }
    if (animSparkle(2))
    {
      currentState = STATE_SCANNER;
      initNextAnim = true;
    }
    break;

  case STATE_SCANNER:
    if (!showScanner)
    {
      currentState = STATE_TEMPS;
      initNextAnim = true;
      break;
    }
    if (initNextAnim)
    {
      animScanner(durationScannerLoops, true);
      initNextAnim = false;
    }
    if (animScanner(durationScannerLoops))
    {
      currentState = STATE_TEMPS;
      initNextAnim = true;
    }
    break;

  case STATE_TEMPS:
    if (!showTemps)
    {
      currentState = STATE_MESSAGE;
      initNextAnim = true;
      break;
    }
    if (initNextAnim)
    {
      displayExternalTemps(durationTemps, true);
      initNextAnim = false;
    }
    if (displayExternalTemps(durationTemps))
    {
      currentState = STATE_MESSAGE;
      initNextAnim = true;
    }
    break;

  case STATE_MESSAGE:
    if (!showMsg)
    {
      currentState = STATE_CLOCK;
      initNextAnim = true;
      break;
    }
    if (initNextAnim)
    {
      animScroll(customMessage.c_str(), true);
      initNextAnim = false;
    }
    if (animScroll(customMessage.c_str()))
    {
      currentState = STATE_CLOCK;
      initNextAnim = true;
    }
    break;

  case STATE_WIPE_TO_TEMPS:
    if (initNextAnim)
    {
      animWipereverse(10, true);
      initNextAnim = false;
    }
    if (animWipereverse(10))
    {
      currentState = STATE_SHOW_TEMPS;
      initNextAnim = true;
    }
    break;

  case STATE_SHOW_TEMPS:
    if (initNextAnim)
    {
      displayExternalTemps(5000, true);
      initNextAnim = false;
    }
    if (displayExternalTemps(5000))
    {
      currentState = STATE_WIPE_TO_CLOCK;
      initNextAnim = true;
    }
    break;

  case STATE_WIPE_TO_CLOCK:
    if (initNextAnim)
    {
      animWipe(10, true);
      initNextAnim = false;
    }
    if (animWipe(10))
    {
      currentState = STATE_CLOCK;
      initNextAnim = true;
    }
    break;

  case STATE_WIPE_TO_MSG:
    if (initNextAnim)
    {
      animWipereverse(10, true);
      initNextAnim = false;
    }
    if (animWipereverse(10))
    {
      currentState = STATE_SHOW_MSG;
      initNextAnim = true;
    }
    break;

  case STATE_SHOW_MSG:
    if (initNextAnim)
    {
      displayCustomMessage(customMessageDuration, customMessage.c_str(), true);
      initNextAnim = false;
    }
    if (displayCustomMessage(customMessageDuration, customMessage.c_str()))
    {
      currentState = STATE_WIPE_TO_CLOCK;
      initNextAnim = true;
    }
    break;
  }
}