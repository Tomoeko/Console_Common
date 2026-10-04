#ifndef CC_DISPLAY_SETTINGS_H
#define CC_DISPLAY_SETTINGS_H

#include <stdbool.h>

typedef struct {
    bool antialiasing;
} CcDisplaySettings;

/* Missing files are created with antialiasing enabled. Invalid settings leave
 * the caller's state unchanged. Loading happens before graphics initialization. */
bool cc_display_settings_load(CcDisplaySettings *settings, const char *path);
/* Uses Files/display.json beside the project, including executable launches. */
bool cc_display_settings_load_project(CcDisplaySettings *settings);

#endif
