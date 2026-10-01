#ifndef CONSOLE_COMMON_JSON_H
#define CONSOLE_COMMON_JSON_H

#include <stdbool.h>
#include <stddef.h>

typedef enum CcJsonType {
    CC_JSON_OBJECT,
    CC_JSON_ARRAY,
    CC_JSON_STRING,
    CC_JSON_NUMBER,
    CC_JSON_BOOLEAN,
    CC_JSON_NULL
} CcJsonType;

typedef struct CcJsonToken {
    CcJsonType type;
    size_t start;
    size_t end;
    size_t next;
    size_t children;
} CcJsonToken;

typedef struct CcJson {
    char *source;
    CcJsonToken *tokens;
    size_t length;
    size_t count;
    size_t capacity;
} CcJson;

bool cc_json_load(CcJson *json, const char *path, size_t max_bytes);
/* Parse a caller-owned byte range after making a private copy. */
bool cc_json_parse(CcJson *json, const char *source, size_t length);
void cc_json_free(CcJson *json);
size_t cc_json_member(const CcJson *json, size_t object, const char *key);
size_t cc_json_index(const CcJson *json, size_t array, size_t index);
bool cc_json_equals(const CcJson *json, size_t token, const char *value);
bool cc_json_copy(const CcJson *json, size_t token, char *destination, size_t capacity);
/* Decode text for C-string consumers, rejecting embedded NUL characters. */
bool cc_json_copy_text(const CcJson *json, size_t token, char *destination,
                       size_t capacity);
bool cc_json_integer(const CcJson *json, size_t token, int *value);

#define CC_JSON_INVALID ((size_t)-1)

#endif
