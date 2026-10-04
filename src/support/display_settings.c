#include "console_common/support/display_settings.h"
#include "console_common/support/host.h"
#include "console_common/support/json.h"
#include "console_common/support/regular_file.h"
#include "support/atomic_file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool cc_display_settings_load(CcDisplaySettings *settings, const char *path) {
    if (!settings || !path)
        return false;
    CcDisplaySettings pending = {.antialiasing = true};
    char *contents = NULL;
    size_t length = 0;
    CcRegularFileStatus status = cc_regular_file_read(path, 4096, &contents, &length);
    if (status == CC_REGULAR_FILE_MISSING) {
        static const char defaults[] = "{\n    \"antialiasing\": true\n}\n";
        if (!cc_atomic_file_replace(path, defaults, sizeof(defaults) - 1))
            return false;
        *settings = pending;
        return true;
    }
    if (status != CC_REGULAR_FILE_OK)
        return false;
    CcJson json = {0};
    bool okay = cc_json_parse(&json, contents, length);
    free(contents);
    if (okay) {
        size_t token = cc_json_member(&json, 0, "antialiasing");
        okay = json.tokens[0].type == CC_JSON_OBJECT && json.tokens[0].children == 1 &&
               token != CC_JSON_INVALID && json.tokens[token].type == CC_JSON_BOOLEAN;
        if (okay)
            pending.antialiasing = json.source[json.tokens[token].start] == 't';
    }
    cc_json_free(&json);
    if (okay)
        *settings = pending;
    return okay;
}

bool cc_display_settings_load_project(CcDisplaySettings *settings) {
    char *root = cc_host_project_directory("Files");
    if (!root)
        return false;
    size_t capacity = strlen(root) + sizeof("/Files/display.json");
    char *path = malloc(capacity);
    bool okay = false;
    if (path) {
        snprintf(path, capacity, "%s/Files/display.json", root);
        okay = cc_display_settings_load(settings, path);
    }
    free(path);
    free(root);
    return okay;
}
