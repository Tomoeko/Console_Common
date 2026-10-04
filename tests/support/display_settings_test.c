#include "console_common/support/display_settings.h"
#include "console_common/support/host.h"
#include "support/atomic_file.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void write_setting(const char *path, const char *source) {
    assert(cc_atomic_file_replace(path, source, strlen(source)));
}

int main(int argc, char **argv) {
    assert(argc == 2);
    char path[4096];
    int length = snprintf(path, sizeof(path), "%s/display.json", argv[1]);
    assert(length > 0 && (size_t)length < sizeof(path));
    remove(path);
    CcDisplaySettings settings = {.antialiasing = false};
    assert(cc_display_settings_load(&settings, path) && settings.antialiasing);
    settings.antialiasing = false;
    assert(cc_display_settings_load(&settings, path) && settings.antialiasing);
    write_setting(path, "{\"antialiasing\":false}");
    assert(cc_display_settings_load(&settings, path) && !settings.antialiasing);
    const char *invalid[] = {"{}",
                             "{\"antialiasing\":1}",
                             "{\"antialiasing\":\"true\"}",
                             "{\"antialiasing\":true,\"antialiasing\":false}",
                             "{\"antialiasing\":true,\"unknown\":false}",
                             "[true]",
                             "{broken}"};
    for (unsigned index = 0; index < sizeof(invalid) / sizeof(invalid[0]); ++index) {
        write_setting(path, invalid[index]);
        assert(!cc_display_settings_load(&settings, path));
        assert(!settings.antialiasing);
    }
    assert(!cc_display_settings_load(NULL, path));
    assert(!cc_display_settings_load(&settings, NULL));
    assert(remove(path) == 0);
    return 0;
}
