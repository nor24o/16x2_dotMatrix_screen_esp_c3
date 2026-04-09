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

// ==========================================
// 1. CONFIGURATION & STATE
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

// --- DYNAMIC DATA & PREFERENCES ---
Preferences preferences;
WiFiUDP udp;
const unsigned int localUdpPort = 8888;

String targetCityName = "Sauca";
String targetCountryCode = "RO";
String targetRegion = "Satu Mare";

float externalTemps[4] = {-999.0, -999.0, -999.0, -999.0};

// --- PLAYLIST TOGGLES ---
bool showClock = true;
bool showTypewriter = true;
bool showSparkle = true;
bool showScanner = true;
bool showTemps = true;
bool showMsg = false;
String customMessage = "HELLO";
unsigned long customMessageDuration = 10000; // Default 10 seconds

// --- STATE MACHINE DEFINITIONS ---
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
  STATE_SHOW_MSG // <-- Added for custom alerts
};
AppState currentState = STATE_BOOT_IP;
bool initNextAnim = true;

// ==========================================
// 2. PIN DEFINITIONS & MACROS
// ==========================================
#define DATA_PIN 4
#define CLK_PIN 21
#define STR_PIN 3
#define OE_PIN 7
#define ADDR_A 2
#define ADDR_B 6
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
// 3. MATRIX SETTINGS & DOUBLE BUFFERING
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

uint8_t drawBuffer[ROWS][BUFFER_BYTES];
volatile uint8_t displayBuffer[ROWS][BUFFER_BYTES];

hw_timer_t *timer = NULL;
TaskHandle_t displayTaskHandle = NULL;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

// ==========================================
// 4. FONTS
// ==========================================
const uint8_t FONT_BIG_DIGITS[][4] = {
    {0x7F, 0x41, 0x41, 0x7F}, {0x00, 0x42, 0x7F, 0x40}, {0x79, 0x49, 0x49, 0x4F}, {0x41, 0x49, 0x49, 0x7F}, {0x0F, 0x08, 0x08, 0x7F}, {0x4F, 0x49, 0x49, 0x79}, {0x7F, 0x49, 0x49, 0x79}, {0x01, 0x01, 0x01, 0x7F}, {0x7F, 0x49, 0x49, 0x7F}, {0x4F, 0x49, 0x49, 0x7F}, {0x00, 0x60, 0x60, 0x00}, {0x08, 0x08, 0x08, 0x08}, {0x00, 0x00, 0x00, 0x00}};
const uint8_t ICON_SUN_MOD[] = {0x42, 0x24, 0x3C, 0x24, 0x42};
const uint8_t ICON_CLOUD_MOD[] = {0x0C, 0x1E, 0x3F, 0x33, 0x1E};
const uint8_t ICON_RAIN_MOD[] = {0x0C, 0x1E, 0x3B, 0x33, 0x20};
const uint8_t ICON_THUNDER_MOD[] = {0x06, 0x1C, 0x38, 0x0E, 0x03};
const uint8_t ICON_MIST_MOD[] = {0x24, 0x49, 0x24, 0x49, 0x24};
const uint8_t FONT_TEXT[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, {0x7E, 0x11, 0x11, 0x11, 0x7E}, {0x7F, 0x49, 0x49, 0x49, 0x36}, {0x3E, 0x41, 0x41, 0x41, 0x22}, {0x7F, 0x41, 0x41, 0x22, 0x1C}, {0x7F, 0x49, 0x49, 0x49, 0x41}, {0x7F, 0x09, 0x09, 0x09, 0x01}, {0x3E, 0x41, 0x49, 0x49, 0x7A}, {0x7F, 0x08, 0x08, 0x08, 0x7F}, {0x00, 0x41, 0x7F, 0x41, 0x00}, {0x20, 0x40, 0x41, 0x3F, 0x01}, {0x7F, 0x08, 0x14, 0x22, 0x41}, {0x7F, 0x40, 0x40, 0x40, 0x40}, {0x7F, 0x02, 0x0C, 0x02, 0x7F}, {0x7F, 0x04, 0x08, 0x10, 0x7F}, {0x3E, 0x41, 0x41, 0x41, 0x3E}, {0x7F, 0x09, 0x09, 0x09, 0x06}, {0x3E, 0x41, 0x51, 0x21, 0x5E}, {0x7F, 0x09, 0x19, 0x29, 0x46}, {0x46, 0x49, 0x49, 0x49, 0x31}, {0x01, 0x01, 0x7F, 0x01, 0x01}, {0x3F, 0x40, 0x40, 0x40, 0x3F}, {0x1F, 0x20, 0x40, 0x20, 0x1F}, {0x3F, 0x40, 0x38, 0x40, 0x3F}, {0x63, 0x14, 0x08, 0x14, 0x63}, {0x07, 0x08, 0x70, 0x08, 0x07}, {0x61, 0x51, 0x49, 0x45, 0x43}, {0x3E, 0x41, 0x41, 0x41, 0x3E}, {0x00, 0x42, 0x7F, 0x40, 0x00}, {0x42, 0x61, 0x51, 0x49, 0x46}, {0x21, 0x41, 0x45, 0x4B, 0x31}, {0x18, 0x14, 0x12, 0x7F, 0x10}, {0x27, 0x45, 0x45, 0x45, 0x39}, {0x3C, 0x4A, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03}, {0x36, 0x49, 0x49, 0x49, 0x36}, {0x06, 0x49, 0x49, 0x29, 0x1E}};

// ==========================================
// 5. GRAPHICS ENGINE & HELPERS
// ==========================================
void clearBuffer() { memset((void *)drawBuffer, 0, sizeof(drawBuffer)); }

void show()
{
  portENTER_CRITICAL(&timerMux);
  memcpy((void *)displayBuffer, (void *)drawBuffer, sizeof(displayBuffer));
  portEXIT_CRITICAL(&timerMux);
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

void drawTextAt(int x, const char *msg)
{
  int len = strlen(msg);
  for (int i = 0; i < len; i++)
  {
    char c = msg[i];
    if (c >= 'a' && c <= 'z')
      c -= 32;
    if (c == '.')
    {
      setPixel(x + 1, 7, 1);
      setPixel(x + 2, 7, 1);
    }
    else if (c == '%')
    {
      setPixel(x, 1, 1);
      setPixel(x + 3, 1, 1);
      setPixel(x + 1, 3, 1);
      setPixel(x + 2, 4, 1);
      setPixel(x, 6, 1);
      setPixel(x + 3, 6, 1);
    }
    else if (c == ':')
    {
      setPixel(x + 1, 2, 1);
      setPixel(x + 1, 5, 1);
      setPixel(x + 2, 2, 1);
      setPixel(x + 2, 5, 1);
    }
    else
    {
      uint8_t idx = getCharIdxText(c);
      for (int col = 0; col < 5; col++)
      {
        uint8_t data = FONT_TEXT[idx][col];
        for (int r = 0; r < 8; r++)
          if (data & (1 << r))
            setPixel(x + col, r, 1);
      }
    }
    x += VIRTUAL_MOD_WIDTH;
  }
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
    return "OVCST";
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

bool isNightTime() {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    // Case 1: Night spans across midnight (e.g., 22:00 to 07:00)
    if (NIGHT_START_HOUR > NIGHT_END_HOUR) {
      if (timeinfo.tm_hour >= NIGHT_START_HOUR || timeinfo.tm_hour < NIGHT_END_HOUR) {
        return true;
      }
    } 
    // Case 2: Night is within the same calendar day (e.g., 01:00 to 05:00)
    else if (NIGHT_START_HOUR < NIGHT_END_HOUR) {
      if (timeinfo.tm_hour >= NIGHT_START_HOUR && timeinfo.tm_hour < NIGHT_END_HOUR) {
        return true;
      }
    }
    // Case 3: Start and End are exactly the same (e.g., 0 to 0) -> Never night time
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
              float temp = weatherDoc["current"]["temperature_2m"];
              int hum = weatherDoc["current"]["relative_humidity_2m"];
              int wmoCode = weatherDoc["current"]["weather_code"];
              const char *condition = getWeatherCondition(wmoCode);
              sprintf(liveWeatherString, "%s: %s %dC   HUMIDITY %d%%   ", officialName.c_str(), condition, (int)round(temp), hum);
              String shortName = officialName.substring(0, 2);
              const char *shortCond = getShortWeatherCondition(wmoCode);
              sprintf(nightWeatherString, "%s:%s T%dC H%d%%", shortName.c_str(), shortCond, (int)round(temp), hum);
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
// 6.5. UDP COMMAND HANDLER (JSON PARSER)
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
        if (!doc["bright"].isNull()) {
          BRIGHTNESS = constrain(doc["bright"].as<int>(), 0, 1000);
          preferences.putInt("bright", BRIGHTNESS);
        }
        if (!doc["nbright"].isNull()) {
          NIGHT_BRIGHTNESS = constrain(doc["nbright"].as<int>(), 0, 1000);
          preferences.putInt("nbright", NIGHT_BRIGHTNESS);
        }
        if (!doc["nstart"].isNull()) {
          NIGHT_START_HOUR = constrain(doc["nstart"].as<int>(), 0, 23);
          preferences.putInt("nstart", NIGHT_START_HOUR);
        }
        if (!doc["nend"].isNull()) {
          NIGHT_END_HOUR = constrain(doc["nend"].as<int>(), 0, 23);
          preferences.putInt("nend", NIGHT_END_HOUR);
        }
        if (!doc["gmtoff"].isNull()) {
          gmtOffset_sec = constrain(doc["gmtoff"].as<long>(), -43200, 50400); // UTC-12 to UTC+14
          preferences.putLong("gmtoff", gmtOffset_sec);
          configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
        }
        if (!doc["dstoff"].isNull()) {
          daylightOffset_sec = constrain(doc["dstoff"].as<int>(), 0, 7200); // 0 to 2 hours
          preferences.putInt("dstoff", daylightOffset_sec);
          configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
        }
        if (!doc["city"].isNull()) {
          targetCityName = doc["city"].as<String>().substring(0, 30); // Max 30 chars
          preferences.putString("city", targetCityName);
          lastWeatherUpdate = 0;
        }
        if (!doc["country"].isNull()) {
          targetCountryCode = doc["country"].as<String>().substring(0, 3); // Max 3 chars
          preferences.putString("country", targetCountryCode);
          lastWeatherUpdate = 0;
        }
        if (!doc["region"].isNull()) {
          targetRegion = doc["region"].as<String>().substring(0, 30); // Max 30 chars
          preferences.putString("region", targetRegion);
          lastWeatherUpdate = 0;
        }
      }
      else if (cmd && strcmp(cmd, "playlist") == 0)
      {
        if (!doc["clock"].isNull()) { showClock = doc["clock"]; preferences.putBool("sClk", showClock); }
        if (!doc["typewriter"].isNull()) { showTypewriter = doc["typewriter"]; preferences.putBool("sTyp", showTypewriter); }
        if (!doc["sparkle"].isNull()) { showSparkle = doc["sparkle"]; preferences.putBool("sSprk", showSparkle); }
        if (!doc["scanner"].isNull()) { showScanner = doc["scanner"]; preferences.putBool("sScan", showScanner); }
        if (!doc["temps"].isNull()) { showTemps = doc["temps"]; preferences.putBool("sTmp", showTemps); }
        if (!doc["msg"].isNull()) { showMsg = doc["msg"]; preferences.putBool("sMsg", showMsg); }

        // Failsafe: Prevent infinite locking loop if user turns everything off
        if (!showClock && !showTypewriter && !showSparkle && !showScanner && !showTemps && !showMsg)
        {
          showClock = true;
        }

        JsonDocument replyDoc;
        replyDoc["status"] = "PLAYLIST_UPDATED";
        udp.beginPacket(udp.remoteIP(), udp.remotePort());
        serializeJson(replyDoc, udp);
        udp.endPacket();
      }
      else if (cmd && strcmp(cmd, "message") == 0)
      {
        if (!doc["text"].isNull())
          customMessage = doc["text"].as<String>().substring(0, 100); // Prevent RAM overflow (Max 100 chars)

        // Constrain time between 1 second and 5 minutes
        if (!doc["time"].isNull())
          customMessageDuration = constrain(doc["time"].as<long>(), 1000, 300000);
        else
          customMessageDuration = 10000;

        // Force the state machine to instantly wipe and show the message
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
        // Constrain temperatures to avoid rendering garbage if sensor fails and sends 9999
        if (!doc["t1"].isNull()) externalTemps[0] = constrain(doc["t1"].as<float>(), -99.9, 99.9);
        if (!doc["t2"].isNull()) externalTemps[1] = constrain(doc["t2"].as<float>(), -99.9, 99.9);
        if (!doc["t3"].isNull()) externalTemps[2] = constrain(doc["t3"].as<float>(), -99.9, 99.9);
        if (!doc["t4"].isNull()) externalTemps[3] = constrain(doc["t4"].as<float>(), -99.9, 99.9);

        currentState = STATE_WIPE_TO_TEMPS;
        initNextAnim = true;

        JsonDocument replyDoc;
        replyDoc["status"] = "TEMPS_RECEIVED";
        udp.beginPacket(udp.remoteIP(), udp.remotePort());
        serializeJson(replyDoc, udp);
        udp.endPacket();
      }
            else if(cmd && strcmp(cmd, "info") == 0)
      {
        JsonDocument replyDoc;
        replyDoc["ip"] = ipString;
        replyDoc["city"] = targetCityName;
        replyDoc["country"] = targetCountryCode;
        replyDoc["region"] = targetRegion;
        replyDoc["bright"] = BRIGHTNESS;
        replyDoc["nbright"] = NIGHT_BRIGHTNESS;
        replyDoc["nstart"] = NIGHT_START_HOUR;
        replyDoc["nend"] = NIGHT_END_HOUR;
        replyDoc["gmtoff"] = gmtOffset_sec;
        replyDoc["dstoff"] = daylightOffset_sec;
        replyDoc["showClock"] = showClock;
        replyDoc["showTypewriter"] = showTypewriter;
        replyDoc["showSparkle"] = showSparkle;
        replyDoc["showScanner"] = showScanner;
        replyDoc["showTemps"] = showTemps;
        replyDoc["showMsg"] = showMsg;

        udp.beginPacket(udp.remoteIP(), udp.remotePort());
        serializeJson(replyDoc, udp);
        udp.endPacket();
    }
    }
  }
}
// ==========================================
// 7. NON-BLOCKING ANIMATIONS
// ==========================================

bool animTimeWeather(int durationMs, const char *weatherMsg, bool init = false)
{
  static unsigned long startMillis = 0;
  static unsigned long prevScroll = 0;
  static unsigned long prevBlink = 0;
  static int scrollX = 0;
  static bool blinkState = true;

  static int lastMinute = -1;
  static char cachedTimeStr[16] = "            ";

  int screen1Start = getVirtualModuleStart(1, 0);
  bool nightMode = isNightTime();

  if (init)
  {
    startMillis = millis();
    prevScroll = millis();
    prevBlink = millis();
    scrollX = VIRTUAL_COLS;
    blinkState = true;
    lastMinute = -1;
    return false;
  }

  if (millis() - startMillis >= durationMs)
  {
    if (nightMode)
      startMillis = millis();
    else
      return true;
  }

  bool needsRedraw = false;
  if (millis() - prevBlink >= 500)
  {
    prevBlink = millis();
    blinkState = !blinkState;
    needsRedraw = true;
  }

  if (!nightMode)
  {
    if (millis() - prevScroll >= 30)
    {
      prevScroll = millis();
      scrollX--;
      int len = strlen(weatherMsg);
      if (scrollX < screen1Start - (len * VIRTUAL_MOD_WIDTH))
        scrollX = VIRTUAL_COLS;
      needsRedraw = true;
    }
  }

  if (needsRedraw)
  {
    clearBuffer();

    struct tm timeinfo;
    if (getLocalTime(&timeinfo))
    {
      if (timeinfo.tm_min != lastMinute)
      {
        lastMinute = timeinfo.tm_min;
        strftime(cachedTimeStr, sizeof(cachedTimeStr), "%d %b %H %M", &timeinfo);
      }
    }

    for (int i = 0; i < 12; i++)
      drawModuleChar(0, i + 2, cachedTimeStr[i]);
    if (blinkState)
      drawModuleChar(0, 9 + 2, ':');

    if (nightMode)
    {
      int len = strlen(nightWeatherString);
      for (int i = 0; i < len && i < 16; i++)
        drawModuleChar(1, i, nightWeatherString[i]);
    }
    else
    {
      int len = strlen(weatherMsg);
      int x = scrollX;
      for (int i = 0; i < len; i++)
      {
        char c = weatherMsg[i];
        if (c >= 'a' && c <= 'z')
          c -= 32;
        if (c == '%')
        {
          for (int col = 0; col < 4; col++)
          {
            int vx = x + col;
            if (vx >= screen1Start && vx < VIRTUAL_COLS)
            {
              if (col == 0)
              {
                setPixel(vx, 1, 1);
                setPixel(vx, 6, 1);
              }
              if (col == 1)
                setPixel(vx, 3, 1);
              if (col == 2)
                setPixel(vx, 4, 1);
              if (col == 3)
              {
                setPixel(vx, 1, 1);
                setPixel(vx, 6, 1);
              }
            }
          }
        }
        else if (c == ':')
        {
          int vx = x;
          if (vx >= screen1Start && vx < VIRTUAL_COLS)
          {
            setPixel(vx + 1, 2, 1);
            setPixel(vx + 1, 5, 1);
            setPixel(vx + 2, 2, 1);
            setPixel(vx + 2, 5, 1);
          }
        }
        else if (c == '.')
        {
          int vx = x;
          if (vx >= screen1Start && vx < VIRTUAL_COLS)
          {
            setPixel(vx + 1, 7, 1);
            setPixel(vx + 2, 7, 1);
          }
        }
        else
        {
          uint8_t idx = getCharIdxText(c);
          for (int col = 0; col < 5; col++)
          {
            int vx = x + col;
            if (vx >= screen1Start && vx < VIRTUAL_COLS)
            {
              uint8_t data = FONT_TEXT[idx][col];
              for (int r = 0; r < 8; r++)
                if (data & (1 << r))
                  setPixel(vx, r, 1);
            }
          }
        }
        x += VIRTUAL_MOD_WIDTH;
      }
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
    setPixel(random(0, VIRTUAL_COLS), random(0, ROWS), 0);
    show();
    count++;
    if (count >= 800)
    {
      clearBuffer();
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
    i = 0;
    dir = 1;
    prevMillis = millis();
    return false;
  }
  if (millis() - prevMillis >= 5)
  {
    prevMillis = millis();
    clearBuffer();
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
    else if (dir == -1 && i <= 0)
    {
      dir = 1;
      i = 0;
      currentLoop++;
      if (currentLoop >= loops)
      {
        clearBuffer();
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
  if (millis() - prevMillis >= 20)
  {
    prevMillis = millis();
    clearBuffer();
    drawTextAt(x, txt);
    show();
    x--;
    if (x <= -(len * VIRTUAL_MOD_WIDTH))
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
    clearBuffer();
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
    if (millis() - prevMillis >= 80)
    {
      prevMillis = millis();
      int startMod = (TOTAL_MODULES > len) ? (TOTAL_MODULES - len) / 2 : 0;
      int startX = startMod * VIRTUAL_MOD_WIDTH;
      char buf[2] = {msg[charIndex], '\0'};
      drawTextAt(startX + (charIndex * VIRTUAL_MOD_WIDTH), buf);
      show();
      charIndex++;
      if (charIndex >= len)
        isWaiting = true;
    }
  }
  return false;
}

bool displayStaticScreen(bool init = false)
{
  static unsigned long prevMillis = 0;
  if (init)
  {
    clearBuffer();

    float t1 = (externalTemps[0] != -999.0) ? externalTemps[0] : 24.5;
    float t2 = (externalTemps[1] != -999.0) ? externalTemps[1] : 18.2;
    float t3 = (externalTemps[2] != -999.0) ? externalTemps[2] : 12.0;
    float t4 = (externalTemps[3] != -999.0) ? externalTemps[3] : -5.5;

    drawTemp(0, 0, t1);
    drawIcon(0, 5, ICON_SUN_MOD);
    drawTemp(0, 9, t2);
    drawIcon(0, 14, ICON_CLOUD_MOD);
    drawTemp(1, 0, t3);
    drawIcon(1, 5, ICON_RAIN_MOD);
    drawTemp(1, 9, t4);
    drawIcon(1, 14, ICON_THUNDER_MOD);

    show();
    prevMillis = millis();
    return false;
  }
  if (millis() - prevMillis >= 5000)
    return true;
  return false;
}

bool displayExternalTemps(int durationMs, bool init = false)
{
  static unsigned long startMillis = 0;
  if (init)
  {
    clearBuffer();
    float t1 = (externalTemps[0] != -999.0) ? externalTemps[0] : 0.0;
    float t2 = (externalTemps[1] != -999.0) ? externalTemps[1] : 0.0;
    float t3 = (externalTemps[2] != -999.0) ? externalTemps[2] : 0.0;
    float t4 = (externalTemps[3] != -999.0) ? externalTemps[3] : 0.0;

    drawTemp(0, 0, t1);
    drawIcon(0, 5, ICON_SUN_MOD);
    drawTemp(0, 9, t2);
    drawIcon(0, 14, ICON_CLOUD_MOD);
    drawTemp(1, 0, t3);
    drawIcon(1, 5, ICON_RAIN_MOD);
    drawTemp(1, 9, t4);
    drawIcon(1, 14, ICON_THUNDER_MOD);

    show();
    startMillis = millis();
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
    col = 0;
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
    if (col < 0)
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
    animScroll(msg, true); // Initialize the scroller
    return false;
  }

  // If the text finishes scrolling before the time is up, restart it seamlessly
  if (animScroll(msg))
  {
    animScroll(msg, true);
  }

  // Once the requested time has passed, tell the state machine to move on
  if (millis() - startMillis >= durationMs)
  {
    return true;
  }
  return false;
}

// ==========================================
// 8. THE HARDWARE TIMER ISR
// ==========================================
void IRAM_ATTR onTimer()
{
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  vTaskNotifyGiveFromISR(displayTaskHandle, &xHigherPriorityTaskWoken);
  if (xHigherPriorityTaskWoken)
  {
    portYIELD_FROM_ISR();
  }
}

// ==========================================
// 9. THE HYBRID DISPLAY TASK (NO SPI)
// ==========================================
void displayRefreshTask(void *pvParameters)
{
  uint8_t currentRow = 0;

  for (;;)
  {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    portENTER_CRITICAL(&timerMux);
    OE_HIGH(); // 0. Turn off LEDs

    // 1. SHIFT DATA FIRST
    // Do this while the old address is technically selected, but the screen is blanked.
    for (int b = 0; b < BUFFER_BYTES; b++)
    {
      uint8_t data = displayBuffer[currentRow][b];
      for (int i = 0; i < 8; i++)
      {
        CLK_LOW();
        if (data & 1)
          DATA_HIGH();
        else
          DATA_LOW();
        data >>= 1;

        // Add a micro-delay to let the data stabilize before clocking
        asm volatile("nop");
        CLK_HIGH();
        // Add a micro-delay so the panel actually registers the clock tick
        asm volatile("nop");
      }
    }

    // 2. SET ROW ADDRESS SECOND
    // The data lines are quiet now, so the address change won't get corrupted
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

    // 3. LATCH THIRD
    STR_HIGH();
    asm volatile("nop");
    asm volatile("nop");
    asm volatile("nop");
    asm volatile("nop");
    STR_LOW();

    // Give the silicon a tiny fraction of time to lock everything in
    delayMicroseconds(2);

    // 4. ILLUMINATE
    if (currentBrightness > 0)
    {
      OE_LOW();
      delayMicroseconds(currentBrightness);
      OE_HIGH();
    }

    portEXIT_CRITICAL(&timerMux);

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
  
  gmtOffset_sec = constrain(preferences.getLong("gmtoff", 7200), -43200, 50400);
  daylightOffset_sec = constrain(preferences.getInt("dstoff", 3600), 0, 7200);
  
  targetCityName = preferences.getString("city", "Sauca").substring(0, 30);
  targetCountryCode = preferences.getString("country", "RO").substring(0, 3);
  targetRegion = preferences.getString("region", "Satu Mare").substring(0, 30);

  // Load individual Playlist preferences
  showClock = preferences.getBool("sClk", true);
  showTypewriter = preferences.getBool("sTyp", true);
  showSparkle = preferences.getBool("sSprk", true);
  showScanner = preferences.getBool("sScan", true);
  showTemps = preferences.getBool("sTmp", true);
  showMsg = preferences.getBool("sMsg", false);
  customMessage = preferences.getString("cMsg", "HELLO");

  currentBrightness = BRIGHTNESS;

  pinMode(DATA_PIN, OUTPUT);
  pinMode(CLK_PIN, OUTPUT);
  pinMode(STR_PIN, OUTPUT);
  pinMode(OE_PIN, OUTPUT);
  pinMode(ADDR_A, OUTPUT);
  pinMode(ADDR_B, OUTPUT);
  pinMode(ADDR_C, OUTPUT);
  pinMode(G1_BLANK, OUTPUT);

  OE_HIGH();
  BLANK_HIGH();

  clearBuffer();
  drawTextAt(18, "WIFI...");
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
  ArduinoOTA.begin();

  xTaskCreate(displayRefreshTask, "DisplayTask", 4096, NULL, 5, &displayTaskHandle);

  timer = timerBegin(1000000);
  timerAttachInterrupt(timer, &onTimer);
  timerAlarm(timer, REFRESH_INTERVAL_US, true, 0);

  xTaskCreate(weatherUpdateTask, "WeatherTask", 8192, NULL, 1, NULL);
}

// ==========================================
// 11. MAIN LOOP (CASCADING STATE MACHINE)
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

  // --- CASCADING PLAYLIST ---
  case STATE_CLOCK:
    if (!showClock)
    {
      currentState = STATE_TYPEWRITER;
      initNextAnim = true;
      break;
    }
    if (initNextAnim)
    {
      animTimeWeather(15000, liveWeatherString, true);
      initNextAnim = false;
    }
    if (animTimeWeather(15000, liveWeatherString))
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
      animScanner(2, true);
      initNextAnim = false;
    }
    if (animScanner(2))
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
      displayExternalTemps(8000, true);
      initNextAnim = false;
    }
    if (displayExternalTemps(8000))
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

  // --- UDP INTERRUPT WIPES ---
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
    // Wipe backward to reveal message
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
    // Scroll the message for the requested duration
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