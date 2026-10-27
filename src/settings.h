#pragma once

#include <stdbool.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

void settings_init(void);
void settings_load(bool *flip_h, bool *flip_v, int *brightness);
void settings_save(bool flip_h, bool flip_v, int brightness);
void settings_schedule_save(void);
void settings_commit_if_needed(void);

#ifdef __cplusplus
}
#endif
