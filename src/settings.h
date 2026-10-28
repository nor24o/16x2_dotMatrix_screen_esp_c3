#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    STATE_BOOT_IP = 0,
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
} app_state_t;

typedef struct {
    // Brightness & Night Mode
    int  bright;            // Day brightness (10..800 us, default 400)
    int  nbright;           // Night brightness (10..800 us, default 40)
    int  nstart;            // Night start hour (0..23, default 22)
    int  nend;              // Night end hour (0..23, default 7)
    bool fliph;
    bool flipv;

    // Timezone & DST
    long gmtoff;            // GMT offset in sec (default 7200 = UTC+2)
    int  dstoff;            // DST offset in sec (default 3600 = +1h)

    // Weather Location
    char city[32];          // Default "Sauca"
    char country[4];        // Default "RO"
    char region[32];        // Default "Satu Mare"

    // Playlist Toggles
    bool show_clock;
    bool show_typewriter;
    bool show_sparkle;
    bool show_scanner;
    bool show_temps;
    bool show_msg;

    // Custom Message & Duration
    char custom_msg[101];
    int  custom_msg_duration_ms;

    // Dynamic Runtime Telemetry (RAM)
    float ext_temps[4];
} app_settings_t;

extern app_settings_t g_cfg;
extern char g_ip_str[32];
extern char g_live_weather_str[128];
extern char g_night_weather_str[64];

void settings_init(void);
void settings_load_all(void);
void settings_save_all(void);
void settings_schedule_save(void);
void settings_commit_if_needed(void);
bool settings_is_night_time(void);

// State Machine & Weather Triggers
void app_trigger_state(app_state_t new_state);
app_state_t app_get_state(void);
void app_trigger_weather_refresh(void);

#ifdef __cplusplus
}
#endif
